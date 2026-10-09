return rp.module {
  description = "C++ toolchain",
  requires = { "dev/base", "dev/ide" },
  files = { { dest = "~/.clang-format", content = "BasedOnStyle: Google\n" } },
}
