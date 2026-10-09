return rp.module {
  description = "C++ toolchain",
  packages = { "clang", "cmake" },
  services = { { unit = "distccd.service", scope = "user", state = "enabled", active = true } },
}
