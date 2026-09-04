#include "aurpush/process.hpp"

#include "aurpush/error.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <string>
#include <vector>

namespace aurpush {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::milliseconds kTerminateGrace{2000};

void set_cloexec(int fd) {
  const int flags = fcntl(fd, F_GETFD);
  if (flags >= 0) {
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
}

void set_nonblock(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

void close_fd(int& fd) {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

void close_pair(int (&fds)[2]) {
  close_fd(fds[0]);
  close_fd(fds[1]);
}

// Creates a pipe with both ends marked close-on-exec, so a concurrent fork
// elsewhere cannot inherit and hold open the read end.
bool make_pipe(int (&fds)[2]) {
  if (pipe(fds) != 0) {
    fds[0] = -1;
    fds[1] = -1;
    return false;
  }
  set_cloexec(fds[0]);
  set_cloexec(fds[1]);
  return true;
}

void drain(int fd, std::string& dest, bool& eof) {
  std::array<char, 4096> buf{};
  while (true) {
    const ssize_t n = read(fd, buf.data(), buf.size());
    if (n > 0) {
      dest.append(buf.data(), static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      eof = true;
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    eof = true;
    return;
  }
}

int wait_exit(pid_t pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      throw Error(std::string("waitpid failed: ") + std::strerror(errno));
    }
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return -1;
}

std::vector<char*> c_argv(const std::vector<std::string>& argv) {
  std::vector<char*> out;
  out.reserve(argv.size() + 1);
  for (const auto& a : argv) {
    out.push_back(const_cast<char*>(a.c_str()));
  }
  out.push_back(nullptr);
  return out;
}

// Runs in the forked child and never returns. Reports why exec failed through
// `exec_fd`, which is close-on-exec and so reads as EOF when exec succeeds.
[[noreturn]] void child_exec(const std::vector<std::string>& argv,
                             const std::filesystem::path& cwd,
                             const std::vector<std::pair<std::string, std::string>>& env,
                             int exec_fd) {
  int failure = 0;
  if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
    failure = errno;
  }
  if (failure == 0) {
    for (const auto& [key, value] : env) {
      setenv(key.c_str(), value.c_str(), 1);
    }
    auto cargv = c_argv(argv);
    execvp(cargv[0], cargv.data());
    failure = errno;
  }
  if (exec_fd >= 0) {
    const ssize_t ignored = write(exec_fd, &failure, sizeof(failure));
    static_cast<void>(ignored);
  }
  _exit(127);
}

// Reads the errno the child reported, or 0 when exec succeeded.
int read_exec_error(int fd) {
  int failure = 0;
  std::size_t got = 0;
  while (got < sizeof(failure)) {
    const ssize_t n = read(fd, reinterpret_cast<char*>(&failure) + got, sizeof(failure) - got);
    if (n > 0) {
      got += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  return got == sizeof(failure) ? failure : 0;
}

[[noreturn]] void throw_exec_error(const std::string& program, int err) {
  throw Error("failed to run " + program + ": " + std::strerror(err), 2);
}

int poll_timeout_ms(const std::optional<Clock::time_point>& deadline) {
  if (!deadline) {
    return -1;
  }
  const auto left =
      std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now());
  return left.count() <= 0 ? 0 : static_cast<int>(left.count());
}

}  // namespace

ProcessResult run(const std::vector<std::string>& argv, const ProcessOptions& opts) {
  if (argv.empty()) {
    throw Error("internal error: empty command");
  }

  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  int exec_pipe[2] = {-1, -1};
  if (!make_pipe(out_pipe) || !make_pipe(err_pipe) || !make_pipe(exec_pipe)) {
    const int saved = errno;
    close_pair(out_pipe);
    close_pair(err_pipe);
    close_pair(exec_pipe);
    throw Error(std::string("pipe failed: ") + std::strerror(saved));
  }

  const pid_t pid = fork();
  if (pid < 0) {
    const int saved = errno;
    close_pair(out_pipe);
    close_pair(err_pipe);
    close_pair(exec_pipe);
    throw Error(std::string("fork failed: ") + std::strerror(saved));
  }

  if (pid == 0) {
    // Own process group, so a timeout can reap grandchildren such as the ssh
    // that git spawns. Not done for run_foreground, which must stay in the
    // terminal's foreground group to receive Ctrl-C.
    setpgid(0, 0);
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);
    close(exec_pipe[0]);
    child_exec(argv, opts.cwd, opts.env, exec_pipe[1]);
  }

  setpgid(pid, pid);  // Also set in the parent to avoid a race with the timeout.
  close_fd(out_pipe[1]);
  close_fd(err_pipe[1]);
  close_fd(exec_pipe[1]);

  const int exec_error = read_exec_error(exec_pipe[0]);
  close_fd(exec_pipe[0]);
  if (exec_error != 0) {
    close_pair(out_pipe);
    close_pair(err_pipe);
    wait_exit(pid);
    throw_exec_error(argv[0], exec_error);
  }

  set_nonblock(out_pipe[0]);
  set_nonblock(err_pipe[0]);

  ProcessResult result;
  bool out_eof = false;
  bool err_eof = false;
  std::optional<Clock::time_point> deadline;
  if (opts.timeout.count() > 0) {
    deadline = Clock::now() + opts.timeout;
  }
  bool killed = false;

  while (!out_eof || !err_eof) {
    pollfd fds[2]{};
    nfds_t nfds = 0;
    int out_idx = -1;
    int err_idx = -1;
    if (!out_eof) {
      out_idx = static_cast<int>(nfds);
      fds[nfds].fd = out_pipe[0];
      fds[nfds].events = POLLIN;
      ++nfds;
    }
    if (!err_eof) {
      err_idx = static_cast<int>(nfds);
      fds[nfds].fd = err_pipe[0];
      fds[nfds].events = POLLIN;
      ++nfds;
    }
    const int pr = poll(fds, nfds, poll_timeout_ms(deadline));
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (pr == 0) {
      result.timed_out = true;
      if (!killed) {
        // Politely first, then escalate if the group ignores it.
        kill(-pid, SIGTERM);
        killed = true;
        deadline = Clock::now() + kTerminateGrace;
        continue;
      }
      kill(-pid, SIGKILL);
      break;
    }
    if (out_idx >= 0 && (fds[out_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
      drain(out_pipe[0], result.out, out_eof);
    }
    if (err_idx >= 0 && (fds[err_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
      drain(err_pipe[0], result.err, err_eof);
    }
  }

  close_fd(out_pipe[0]);
  close_fd(err_pipe[0]);

  result.exit_code = wait_exit(pid);
  return result;
}

ProcessResult run(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                  const std::vector<std::pair<std::string, std::string>>& env) {
  ProcessOptions opts;
  opts.cwd = cwd;
  opts.env = env;
  return run(argv, opts);
}

int run_foreground(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
  if (argv.empty()) {
    throw Error("internal error: empty command");
  }

  int exec_pipe[2] = {-1, -1};
  if (!make_pipe(exec_pipe)) {
    throw Error(std::string("pipe failed: ") + std::strerror(errno));
  }

  const pid_t pid = fork();
  if (pid < 0) {
    const int saved = errno;
    close_pair(exec_pipe);
    throw Error(std::string("fork failed: ") + std::strerror(saved));
  }

  if (pid == 0) {
    close(exec_pipe[0]);
    child_exec(argv, cwd, {}, exec_pipe[1]);
  }

  close_fd(exec_pipe[1]);
  const int exec_error = read_exec_error(exec_pipe[0]);
  close_fd(exec_pipe[0]);

  const int code = wait_exit(pid);
  if (exec_error != 0) {
    throw_exec_error(argv[0], exec_error);
  }
  return code;
}

namespace {

// A directory can carry the execute bit, so X_OK alone is not enough to say a
// path names a runnable program.
bool is_executable_file(const std::filesystem::path& path) {
  if (access(path.c_str(), X_OK) != 0) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

}  // namespace

std::optional<std::filesystem::path> find_in_path(std::string_view program) {
  const std::filesystem::path name(program);
  if (name.has_parent_path()) {
    return is_executable_file(name) ? std::optional(name) : std::nullopt;
  }
  const char* path = std::getenv("PATH");
  if (!path || !*path) {
    return std::nullopt;
  }
  std::string_view rest(path);
  while (!rest.empty()) {
    const auto colon = rest.find(':');
    const std::string_view piece = rest.substr(0, colon);
    // A literal empty entry in PATH means the current directory.
    const std::filesystem::path candidate =
        (piece.empty() ? std::filesystem::path(".") : std::filesystem::path(piece)) / name;
    if (is_executable_file(candidate)) {
      return candidate;
    }
    if (colon == std::string_view::npos) {
      break;
    }
    rest.remove_prefix(colon + 1);
  }
  return std::nullopt;
}

void require_tools(const std::vector<std::string>& programs) {
  std::vector<std::string> missing;
  for (const auto& program : programs) {
    if (!find_in_path(program)) {
      missing.push_back(program);
    }
  }
  if (missing.empty()) {
    return;
  }
  std::string msg = missing.size() == 1 ? "required program not found: "
                                        : "required programs not found: ";
  for (std::size_t i = 0; i < missing.size(); ++i) {
    msg += (i == 0 ? "" : ", ");
    msg += missing[i];
  }
  msg += "\nInstall them and try again (aurpush needs git, openssh, and makepkg).";
  throw Error(msg, 2);
}

}  // namespace aurpush
