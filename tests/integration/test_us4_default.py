"""US4 acceptance 1-2: the default profile is used when none is named."""

from conftest import RepdTestCase, main


class DefaultProfile(RepdTestCase):
    fixture = "profiles"

    def test_default_profile_used(self):
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("profile: laptop", r.out)

    def test_no_default_with_several_profiles(self):
        text = (self.sb.config / "repd.lua").read_text().replace("default = true,", "")
        self.sb.write("repd.lua", text)
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_USAGE")
        self.assertIn("laptop, server", r.err)
        self.assertIn("--profile", r.err)

    def test_two_defaults_rejected(self):
        text = (self.sb.config / "repd.lua").read_text().replace(
            'name = "server",', 'name = "server", default = true,')
        self.sb.write("repd.lua", text)
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_CONFIG")
        self.assertIn("laptop and server are both marked default", r.err)

    def test_no_profiles_and_no_modules(self):
        self.sb.write("repd.lua", "")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_USAGE")
        self.assertIn("no modules selected", r.err)


if __name__ == "__main__":
    main()
