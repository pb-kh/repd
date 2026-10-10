"""SC-002: an up-to-date `sync -u` over ~40 modules finishes in under 2 s."""

import statistics
import time

from conftest import RepdTestCase, main


class Performance(RepdTestCase):
    fixture = None

    def test_forty_modules_noop_under_two_seconds(self):
        names = []
        for i in range(40):
            name = f"m{i:02d}"
            names.append(name)
            deps = f'requires = {{ "m{i - 1:02d}" }},' if i % 5 else ""
            self.sb.write(f"modules/{name}/init.lua", f"""
return rp.module {{
  {deps}
  files = {{ {{ dest = "~/.cfg/{name}/a.conf", content = "value={i}\\n" }},
            {{ dest = "~/.cfg/{name}/b.conf", src = "{name}.txt", action = "copy" }} }},
  env = {{ VAR_{i:02d} = "{i}" }},
}}""")
            self.sb.write(f"modules/{name}/files/{name}.txt", f"data {i}\n")
        self.sb.write("repd.lua", "rp.profile { name = 'all', default = true, use = { "
                      + ", ".join(f'"{n}"' for n in names) + " } }\n")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("done: 121 applied", r.out)
        times = []
        for _ in range(5):
            start = time.monotonic()
            r = self.sb.run("sync", "-u")
            times.append(time.monotonic() - start)
            self.assertOk(r)
            self.assertIn("summary: up to date", r.out)
        self.assertLess(statistics.median(times), 2.0, times)


if __name__ == "__main__":
    main()
