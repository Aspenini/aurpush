#include "aurpush/git.hpp"

#include "aurpush/error.hpp"
#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <sstream>

namespace aurpush::git {
namespace {

const std::vector<std::pair<std::string, std::string>> kGitEnv = {
    {"GIT_TERMINAL_PROMPT", "0"},
};

ProcessResult git(const std::filesystem::path& dir, const std::vector<std::string>& args) {
  std::vector<std::string> argv;
  argv.reserve(args.size() + 1);
  argv.emplace_back("git");
  argv.insert(argv.end(), args.begin(), args.end());
  return run(argv, dir, kGitEnv);
}

ProcessResult git_ok(const std::filesystem::path& dir, const std::vector<std::string>& args,
                     const std::string& what) {
  auto result = git(dir, args);
  if (!result.ok()) {
    std::string msg = what;
    const std::string err = trim(result.err.empty() ? result.out : result.err);
    if (!err.empty()) {
      msg += ": " + err;
    }
    throw Error(msg, 2);
  }
  return result;
}

}  // namespace

bool is_repo(const std::filesystem::path& dir) {
  auto result = git(dir, {"rev-parse", "--is-inside-work-tree"});
  return result.ok() && trim(result.out) == "true";
}

void init_master(const std::filesystem::path& dir) {
  auto result = git(dir, {"init", "-b", "master"});
  if (!result.ok()) {
    result = git(dir, {"-c", "init.defaultBranch=master", "init"});
    if (!result.ok()) {
      throw Error("git init failed: " + trim(result.err), 2);
    }
    git(dir, {"checkout", "-b", "master"});
  }
}

std::vector<std::pair<std::string, std::string>> remotes(const std::filesystem::path& dir) {
  auto result = git(dir, {"remote", "-v"});
  std::vector<std::pair<std::string, std::string>> out;
  if (!result.ok()) {
    return out;
  }
  for (const auto& line : split_lines(result.out)) {
    if (line.find("(fetch)") == std::string::npos) {
      continue;
    }
    std::string name;
    std::string url;
    std::istringstream iss(line);
    if (!(iss >> name >> url) || name.empty() || url.empty()) {
      continue;
    }
    out.emplace_back(name, url);
  }
  return out;
}

std::optional<std::string> remote_url(const std::filesystem::path& dir,
                                      const std::string& name) {
  auto result = git(dir, {"remote", "get-url", name});
  if (!result.ok()) {
    return std::nullopt;
  }
  const std::string url = trim(result.out);
  if (url.empty()) {
    return std::nullopt;
  }
  return url;
}

void remote_add(const std::filesystem::path& dir, const std::string& name,
                const std::string& url) {
  git_ok(dir, {"remote", "add", name, url}, "failed to add git remote " + name);
}

void remote_set_url(const std::filesystem::path& dir, const std::string& name,
                    const std::string& url) {
  git_ok(dir, {"remote", "set-url", name, url}, "failed to set git remote " + name);
}

void fetch(const std::filesystem::path& dir, const std::string& remote) {
  auto result = git(dir, {"fetch", "--prune", remote});
  if (result.ok()) {
    return;
  }
  const std::string err = trim(result.err.empty() ? result.out : result.err);
  if (err.find("couldn't find remote ref") != std::string::npos ||
      err.find("no such ref") != std::string::npos ||
      err.find("empty repository") != std::string::npos) {
    return;
  }
  throw Error("failed to fetch from " + remote + (err.empty() ? "" : ": " + err), 2);
}

std::string ls_remote_master(const std::string& url) {
  auto result = run({"git", "ls-remote", url, "refs/heads/master"}, {}, kGitEnv);
  if (!result.ok()) {
    const std::string err = trim(result.err.empty() ? result.out : result.err);
    throw Error("failed to query AUR repository" + (err.empty() ? "" : ": " + err), 2);
  }
  const std::string line = trim(result.out);
  if (line.empty()) {
    return {};
  }
  const auto tab = line.find_first_of(" \t");
  return tab == std::string::npos ? line : line.substr(0, tab);
}

std::optional<std::string> rev_parse(const std::filesystem::path& dir,
                                     const std::string& rev) {
  auto result = git(dir, {"rev-parse", "--verify", rev});
  if (!result.ok()) {
    return std::nullopt;
  }
  const std::string sha = trim(result.out);
  if (sha.empty()) {
    return std::nullopt;
  }
  return sha;
}

bool has_object(const std::filesystem::path& dir, const std::string& sha) {
  return git(dir, {"cat-file", "-e", sha + "^{commit}"}).ok();
}

bool is_ancestor(const std::filesystem::path& dir, const std::string& maybe_ancestor,
                 const std::string& rev) {
  return git(dir, {"merge-base", "--is-ancestor", maybe_ancestor, rev}).ok();
}

std::vector<std::string> tracked_files(const std::filesystem::path& dir) {
  auto head = rev_parse(dir, "HEAD");
  if (!head) {
    return {};
  }
  auto result = git_ok(dir, {"ls-tree", "-r", "--name-only", "HEAD"},
                       "failed to list tracked files");
  std::vector<std::string> files;
  for (const auto& line : split_lines(result.out)) {
    if (!line.empty()) {
      files.push_back(line);
    }
  }
  return files;
}

std::optional<std::string> show_file(const std::filesystem::path& dir,
                                     const std::string& rev, const std::string& file) {
  auto result = git(dir, {"show", rev + ":" + file});
  if (!result.ok()) {
    return std::nullopt;
  }
  return result.out;
}

void checkout_ref(const std::filesystem::path& dir, const std::string& ref) {
  git_ok(dir, {"checkout", "-B", "master", ref}, "failed to check out " + ref);
}

void ensure_master_branch(const std::filesystem::path& dir) {
  auto result = git(dir, {"symbolic-ref", "--short", "HEAD"});
  if (!result.ok()) {
    git(dir, {"checkout", "-B", "master"});
    return;
  }
  const std::string branch = trim(result.out);
  if (branch != "master") {
    git_ok(dir, {"branch", "-M", "master"}, "failed to rename branch to master");
  }
}

void add_force(const std::filesystem::path& dir, const std::vector<std::string>& files) {
  if (files.empty()) {
    return;
  }
  std::vector<std::string> args = {"add", "-f", "--"};
  args.insert(args.end(), files.begin(), files.end());
  git_ok(dir, args, "failed to git add");
}

void rm(const std::filesystem::path& dir, const std::vector<std::string>& files) {
  if (files.empty()) {
    return;
  }
  std::vector<std::string> args = {"rm", "-f", "--"};
  args.insert(args.end(), files.begin(), files.end());
  git_ok(dir, args, "failed to git rm");
}

bool index_has_changes(const std::filesystem::path& dir) {
  return !git(dir, {"diff", "--cached", "--quiet"}).ok();
}

std::vector<std::pair<char, std::string>> cached_changes(const std::filesystem::path& dir) {
  auto result = git_ok(dir, {"diff", "--cached", "--name-status"},
                       "failed to inspect staged changes");
  std::vector<std::pair<char, std::string>> out;
  for (const auto& line : split_lines(result.out)) {
    if (line.size() < 3) {
      continue;
    }
    const char status = line[0];
    const auto tab = line.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    out.emplace_back(status, line.substr(tab + 1));
  }
  return out;
}

void commit(const std::filesystem::path& dir, const std::string& message) {
  git_ok(dir, {"commit", "-m", message}, "failed to create commit");
}

void push(const std::filesystem::path& dir, const std::string& remote,
          const std::string& refspec) {
  git_ok(dir, {"push", remote, refspec}, "failed to push to the AUR");
}

std::optional<std::string> config(const std::filesystem::path& dir, const std::string& key) {
  auto result = git(dir, {"config", "--get", key});
  if (!result.ok()) {
    return std::nullopt;
  }
  const std::string value = trim(result.out);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::string current_branch(const std::filesystem::path& dir) {
  auto result = git(dir, {"symbolic-ref", "--short", "HEAD"});
  return result.ok() ? trim(result.out) : std::string{};
}

}  // namespace aurpush::git
