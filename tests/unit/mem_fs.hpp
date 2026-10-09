// In-memory Fs fake for unit tests of components that touch the filesystem.
#pragma once

#include <map>
#include <string>

#include "platform/fs.hpp"
#include "util/strings.hpp"

namespace repro::testing {

class MemFs final : public Fs {
 public:
  struct Node {
    FileType type = FileType::File;
    std::string data;  // file content or symlink target
    mode_t mode = 0644;
    uid_t uid = 1000;
    gid_t gid = 1000;
  };
  std::map<std::string, Node> nodes;
  int writes = 0;

  void put_file(const std::string& p, std::string data, mode_t mode = 0644) {
    ensure_parents(p);
    nodes[p] = Node{FileType::File, std::move(data), mode};
  }
  void put_link(const std::string& p, std::string target) {
    ensure_parents(p);
    nodes[p] = Node{FileType::Symlink, std::move(target), 0777};
  }
  void put_dir(const std::string& p) {
    ensure_parents(p);
    nodes[p] = Node{FileType::Dir, {}, 0755};
  }

  FileStat lstat(const std::string& p) const override {
    auto it = nodes.find(p);
    if (it == nodes.end()) return {};
    return FileStat{it->second.type, it->second.mode, static_cast<off_t>(it->second.data.size()),
                    it->second.uid, it->second.gid};
  }
  FileStat stat(const std::string& p) const override {
    auto it = nodes.find(p);
    if (it == nodes.end()) return {};
    if (it->second.type == FileType::Symlink) {
      const std::string& t = it->second.data;
      return stat(t.starts_with('/') ? t : dirname(p) + "/" + t);
    }
    return lstat(p);
  }
  Result<std::string> read(const std::string& p, std::size_t max = 64u << 20) const override {
    auto it = nodes.find(p);
    if (it == nodes.end()) return fail(ec::kIo, "no such file " + p, "");
    if (it->second.type == FileType::Symlink) {
      const std::string& t = it->second.data;
      return read(t.starts_with('/') ? t : dirname(p) + "/" + t, max);
    }
    if (it->second.type != FileType::File) return fail(ec::kIo, "not a file " + p, "");
    return it->second.data;
  }
  Result<std::string> readlink(const std::string& p) const override {
    auto it = nodes.find(p);
    if (it == nodes.end() || it->second.type != FileType::Symlink) {
      return fail(ec::kIo, "not a link " + p, "");
    }
    return it->second.data;
  }
  Result<std::vector<std::string>> list(const std::string& dir) const override {
    std::vector<std::string> out;
    const std::string prefix = dir == "/" ? "/" : dir + "/";
    for (const auto& [p, n] : nodes) {
      if (p.starts_with(prefix) && p.find('/', prefix.size()) == std::string::npos) {
        out.push_back(p.substr(prefix.size()));
      }
    }
    if (out.empty() && lstat(dir).type != FileType::Dir) return fail(ec::kIo, "no dir " + dir, "");
    return out;
  }
  Status write_atomic(const std::string& p, std::string_view data,
                      std::optional<mode_t> mode = std::nullopt) override {
    if (lstat(dirname(p)).type != FileType::Dir) return fail(ec::kIo, "no parent for " + p, "");
    ++writes;
    nodes[p] = Node{FileType::File, std::string(data), mode.value_or(0644)};
    return {};
  }
  Status symlink_atomic(const std::string& target, const std::string& p) override {
    if (lstat(dirname(p)).type != FileType::Dir) return fail(ec::kIo, "no parent for " + p, "");
    ++writes;
    nodes[p] = Node{FileType::Symlink, target, 0777};
    return {};
  }
  Status mkdirs(const std::string& p, mode_t = 0755) override {
    if (p.empty()) return {};
    ensure_parents(p + "/x");
    return {};
  }
  Status chmod(const std::string& p, mode_t mode) override {
    nodes[p].mode = mode;
    return {};
  }
  Status chown(const std::string& p, uid_t uid, gid_t gid) override {
    nodes[p].uid = uid;
    nodes[p].gid = gid;
    return {};
  }
  Status rename(const std::string& from, const std::string& to) override {
    auto it = nodes.find(from);
    if (it == nodes.end()) return fail(ec::kIo, "no such file " + from, "");
    nodes[to] = it->second;
    nodes.erase(from);
    return {};
  }
  Status remove(const std::string& p) override {
    nodes.erase(p);
    return {};
  }

 private:
  void ensure_parents(const std::string& p) {
    std::string d = dirname(p);
    while (d != "/" && d != "." && !nodes.contains(d)) {
      nodes[d] = Node{FileType::Dir, {}, 0755};
      d = dirname(d);
    }
  }
};

}  // namespace repro::testing
