// Temporary on-disk config repositories for unit tests.
#pragma once

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace repd::testing {

struct TempRepo {
  std::string root;
  explicit TempRepo(const std::string& tag = "repo") {
    static int counter = 0;
    root = (std::filesystem::temp_directory_path() /
            ("repd-" + tag + "-" + std::to_string(::getpid()) + "-" + std::to_string(counter++)))
               .string();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~TempRepo() { std::filesystem::remove_all(root); }
  TempRepo(const TempRepo&) = delete;
  TempRepo& operator=(const TempRepo&) = delete;

  void write(const std::string& rel, const std::string& content) const {
    const auto p = std::filesystem::path(root) / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << content;
  }
};

}  // namespace repd::testing
