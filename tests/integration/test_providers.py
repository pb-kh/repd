"""SC-006: after one prompt, ten consecutive non-interactive runs never prompt."""

from conftest import RepdTestCase, main


class ProviderStability(RepdTestCase):
    fixture = "providers"

    def test_ten_runs_zero_prompts(self):
        code, out, prompts = self.sb.run_tty("sync", "-u", "dev/c++")
        self.assertEqual((code, prompts), (0, 1), out)
        lock = (self.sb.config / "repd.lock.lua").read_bytes()
        for i in range(10):
            r = self.sb.run("sync", "-u", "dev/c++", "--yes")
            self.assertOk(r, f"run {i}: {r!r}")
            self.assertIn("dev/ide = dev/ide/vim (lock)", r.out)
            self.assertNotIn("choose [", r.out + r.err)
        self.assertEqual((self.sb.config / "repd.lock.lua").read_bytes(), lock)


if __name__ == "__main__":
    main()
