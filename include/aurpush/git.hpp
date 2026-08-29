#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aurpush::git {

bool is_repo(const std::filesystem::path& dir);
void init_master(const std::filesystem::path& dir);

std::vector<std::pair<std::string, std::string>> remotes(const std::filesystem::path& dir);
std::optional<std::string> remote_url(const std::filesystem::path& dir,
                                      const std::string& name);
void remote_add(const std::filesystem::path& dir, const std::string& name,
                const std::string& url);
void remote_set_url(const std::filesystem::path& dir, const std::string& name,
                    const std::string& url);

void fetch(const std::filesystem::path& dir, const std::string& remote);
std::string ls_remote_master(const std::string& url);

std::optional<std::string> rev_parse(const std::filesystem::path& dir,
                                     const std::string& rev);
bool has_object(const std::filesystem::path& dir, const std::string& sha);
bool is_ancestor(const std::filesystem::path& dir, const std::string& maybe_ancestor,
                 const std::string& rev);

std::vector<std::string> tracked_files(const std::filesystem::path& dir);
std::optional<std::string> show_file(const std::filesystem::path& dir,
                                     const std::string& rev, const std::string& file);

void checkout_ref(const std::filesystem::path& dir, const std::string& ref);
void ensure_master_branch(const std::filesystem::path& dir);

void add_force(const std::filesystem::path& dir, const std::vector<std::string>& files);
void rm(const std::filesystem::path& dir, const std::vector<std::string>& files);

bool index_has_changes(const std::filesystem::path& dir);
std::vector<std::pair<char, std::string>> cached_changes(const std::filesystem::path& dir);

void commit(const std::filesystem::path& dir, const std::string& message);
void push(const std::filesystem::path& dir, const std::string& remote,
          const std::string& refspec);

std::optional<std::string> config(const std::filesystem::path& dir, const std::string& key);
std::string current_branch(const std::filesystem::path& dir);

}  // namespace aurpush::git
