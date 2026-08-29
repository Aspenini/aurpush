#include "aurpush/process.hpp"

#include "aurpush/error.hpp"

#include <cerrno>
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

}  // namespace

ProcessResult run(const std::vector<std::string>& argv,
                  const std::filesystem::path& cwd,
                  const std::vector<std::pair<std::string, std::string>>& env) {
  if (argv.empty()) {
    throw Error("internal error: empty command");
  }

  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    throw Error(std::string("pipe failed: ") + std::strerror(errno));
  }
  set_cloexec(out_pipe[0]);
  set_cloexec(out_pipe[1]);
  set_cloexec(err_pipe[0]);
  set_cloexec(err_pipe[1]);

  const pid_t pid = fork();
  if (pid < 0) {
    throw Error(std::string("fork failed: ") + std::strerror(errno));
  }

  if (pid == 0) {
    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) {
        _exit(127);
      }
    }
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);

    for (const auto& [key, value] : env) {
      setenv(key.c_str(), value.c_str(), 1);
    }

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) {
      cargv.push_back(const_cast<char*>(a.c_str()));
    }
    cargv.push_back(nullptr);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }

  close_fd(out_pipe[1]);
  close_fd(err_pipe[1]);
  set_nonblock(out_pipe[0]);
  set_nonblock(err_pipe[0]);

  ProcessResult result;
  bool out_eof = false;
  bool err_eof = false;

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
    const int pr = poll(fds, nfds, -1);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
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

int run_foreground(const std::vector<std::string>& argv, const std::filesystem::path& cwd) {
  if (argv.empty()) {
    throw Error("internal error: empty command");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    throw Error(std::string("fork failed: ") + std::strerror(errno));
  }

  if (pid == 0) {
    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) {
        _exit(127);
      }
    }
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) {
      cargv.push_back(const_cast<char*>(a.c_str()));
    }
    cargv.push_back(nullptr);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }

  return wait_exit(pid);
}

}  // namespace aurpush
