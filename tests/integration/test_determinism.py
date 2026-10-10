"""SC-003: previews are byte-identical across runs and across declaration orders."""

from conftest import RepdTestCase, Sandbox, main

ORDER_A = """
return rp.module {
  env = { ZED = "1", ALPHA = "2", MID = "3", BETA = "4", OMEGA = "5", K10 = "6", K2 = "7" },
  files = { { dest = "~/b", content = "b" }, { dest = "~/a", content = "a" } },
  params = { z = 1, a = "x", m = true },
}"""
ORDER_B = """
return rp.module {
  params = { m = true, a = "x", z = 1 },
  files = { { content = "b", dest = "~/b" }, { content = "a", dest = "~/a" } },
  env = { K2 = "7", K10 = "6", OMEGA = "5", BETA = "4", MID = "3", ALPHA = "2", ZED = "1" },
}"""


class Determinism(RepdTestCase):
    fixture = "profiles"

    def test_two_dry_runs_identical(self):
        a = self.sb.run("sync", "-u", "--dry-run")
        b = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(a)
        self.assertEqual(a.out, b.out)
        self.assertNotIn(str(self.sb.tmp), a.out, "plan must not leak sandbox paths")

    def test_insertion_order_does_not_change_plan_or_fingerprint(self):
        outs = []
        for text in (ORDER_A, ORDER_B):
            sb = Sandbox("files-only")
            self.addCleanup(sb.cleanup)
            sb.write("modules/dotfiles/init.lua", text)
            r = sb.run("sync", "-u", "--dry-run")
            self.assertOk(r)
            outs.append(r.out)
            self.assertOk(sb.run("sync", "-u"))
            outs.append(sb.run("show", "dotfiles").out)
        self.assertEqual(outs[0], outs[2])
        # The canonical declaration is identical; only the source-file hash differs
        # (research R10 hashes the module file bytes, so the fingerprint must differ).
        strip = lambda t: [l for l in t.splitlines() if "source_sha256" not in l]
        self.assertEqual(strip(outs[1]), strip(outs[3]))


if __name__ == "__main__":
    main()
