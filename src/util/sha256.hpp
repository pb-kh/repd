// In-tree SHA-256 (FIPS 180-4), research R7. Used for fingerprints and file digests.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace repd {

class Sha256 {
 public:
  Sha256();
  void update(std::string_view data);
  std::array<std::uint8_t, 32> digest();
  std::string hex();

 private:
  void compress(const std::uint8_t* block);

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

std::string sha256_hex(std::string_view data);

}  // namespace repd
