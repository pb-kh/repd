// FIPS 180-4 / NIST CSRC example vectors for SHA-256 (research R7).
#include <doctest.h>

#include <string>

#include "util/sha256.hpp"

using repd::sha256_hex;

TEST_CASE("sha256: empty string") {
  CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("sha256: abc (one block)") {
  CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("sha256: 448-bit message (two blocks)") {
  CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256: 896-bit message") {
  CHECK(sha256_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopq"
                   "klmnopqrlmnopqrsmnopqrstnopqrstu") ==
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("sha256: one million 'a'") {
  CHECK(sha256_hex(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: incremental updates equal one-shot") {
  repd::Sha256 h;
  const std::string msg = "The quick brown fox jumps over the lazy dog";
  for (char c : msg) h.update(std::string_view(&c, 1));
  CHECK(h.hex() == sha256_hex(msg));
  CHECK(sha256_hex(msg) == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("sha256: block-boundary lengths 55/56/63/64/65") {
  // Reference digests computed independently (coreutils sha256sum).
  CHECK(sha256_hex(std::string(55, 'x')) ==
        "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
  CHECK(sha256_hex(std::string(56, 'x')) ==
        "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
  CHECK(sha256_hex(std::string(63, 'x')) ==
        "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2");
  CHECK(sha256_hex(std::string(64, 'x')) ==
        "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
  CHECK(sha256_hex(std::string(65, 'x')) ==
        "9537c5fdf120482f7d58d25e9ed583f52c02b4e304ea814db1633ad565aed7e9");
  for (std::size_t n : {55u, 56u, 63u, 64u, 65u, 119u, 120u}) {
    std::string s(n, 'q');
    repd::Sha256 a;
    a.update(s.substr(0, n / 2));
    a.update(s.substr(n / 2));
    CHECK(a.hex() == sha256_hex(s));
  }
}
