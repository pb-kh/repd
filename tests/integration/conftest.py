"""Sandboxed integration harness (research R18, Constitution IV).

Every test runs the real `repro` binary against a copy of a fixture repo in a
fresh temp dir: HOME and XDG_* point into it, REPRO_SYSROOT redirects every
system path, REPRO_TEST_ASSUME_ROOT replaces the euid check only when asked,
and logging stubs for useradd/usermod/locale-gen/udevadm come first on PATH.
Nothing here ever touches the live system. Python stdlib only.
"""

import os
import pty
import re
import select
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPRO = os.environ.get("REPRO_BIN", "")
SOURCE_ROOT = Path(os.environ.get("REPRO_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
FIXTURES = SOURCE_ROOT / "tests" / "fixtures"
STUBS = SOURCE_ROOT / "tests" / "integration" / "stubs"


class Result:
    def __init__(self, code, out, err):
        self.code, self.out, self.err = code, out, err

    def __repr__(self):
        return f"<exit {self.code}>\n--- stdout\n{self.out}--- stderr\n{self.err}"


class Sandbox:
    def __init__(self, fixture=None):
        self.tmp = Path(tempfile.mkdtemp(prefix="repro-it-"))
        self.home = self.tmp / "home"
        self.root = self.tmp / "root"
        self.home.mkdir()
        self.root.mkdir()
        self.config = self.home / ".config" / "repro"
        if fixture is not None:
            shutil.copytree(FIXTURES / "repos" / fixture, self.config)
        self.env = {
            "HOME": str(self.home),
            "USER": os.environ.get("USER", "tester"),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_STATE_HOME": str(self.home / ".local" / "state"),
            "REPRO_SYSROOT": str(self.root),
            "PATH": f"{STUBS}:{os.environ.get('PATH', '/usr/bin:/bin')}",
            "NO_COLOR": "1",
            "LANG": "C.UTF-8",
        }

    def cleanup(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write(self, rel, text):
        p = self.config / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        return p

    def add_plugin(self, name):
        dst = self.config / "plugins" / "lua"
        dst.mkdir(parents=True, exist_ok=True)
        shutil.copy(FIXTURES / "plugins" / name, dst / name)

    def env_for(self, root=False, extra=None):
        env = dict(self.env)
        if root:
            env["REPRO_TEST_ASSUME_ROOT"] = "1"
        if extra:
            env.update(extra)
        return env

    def run(self, *args, root=False, extra_env=None, stdin=None, timeout=60):
        p = subprocess.run([REPRO, *args], env=self.env_for(root, extra_env), cwd=self.tmp,
                           input=stdin, capture_output=True, text=True, timeout=timeout,
                           stdin=None if stdin is not None else subprocess.DEVNULL)
        return Result(p.returncode, p.stdout, p.stderr)

    def spawn(self, *args, root=False, extra_env=None):
        return subprocess.Popen([REPRO, *args], env=self.env_for(root, extra_env), cwd=self.tmp,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)

    def run_tty(self, *args, answers=(), root=False, timeout=30):
        """Runs repro on a pseudo-terminal; answers each provider prompt in order.
        Returns (exit code, transcript, number of prompts seen)."""
        pid, fd = pty.fork()
        if pid == 0:  # child
            os.chdir(self.tmp)
            os.execve(REPRO, [REPRO, *args], self.env_for(root))
        transcript, prompts, pending = b"", 0, list(answers)
        deadline = time.time() + timeout
        while time.time() < deadline:
            r, _, _ = select.select([fd], [], [], 0.2)
            if fd in r:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                transcript += chunk
                if transcript.count(b"choose [") > prompts:
                    prompts += 1
                    os.write(fd, (pending.pop(0) if pending else "").encode() + b"\n")
            else:
                done, status = os.waitpid(pid, os.WNOHANG)
                if done:
                    os.close(fd)
                    return os.waitstatus_to_exitcode(status), transcript.decode(errors="replace"), prompts
        try:
            os.close(fd)
        except OSError:
            pass
        _, status = os.waitpid(pid, 0)
        return os.waitstatus_to_exitcode(status), transcript.decode(errors="replace"), prompts

    # filesystem helpers
    def home_path(self, rel):
        return self.home / rel

    def sys_path(self, abs_path):
        return self.root / abs_path.lstrip("/")

    def plugin_dir(self):
        """The system plugin dir (build option -Dplugin_dir) inside the sysroot."""
        d = self.sys_path(os.environ.get("REPRO_PLUGIN_DIR", "/usr/lib/repro"))
        d.mkdir(parents=True, exist_ok=True)
        return d

    def stub_log(self):
        p = self.root / "stub.log"
        return p.read_text() if p.exists() else ""

    def snapshot(self, base):
        """Relative path -> (type, content/target) for everything under base."""
        out = {}
        for p in sorted(Path(base).rglob("*")):
            rel = str(p.relative_to(base))
            if p.is_symlink():
                out[rel] = ("link", os.readlink(p))
            elif p.is_file():
                out[rel] = ("file", p.read_bytes())
            else:
                out[rel] = ("dir", None)
        return out


class ReproTestCase(unittest.TestCase):
    fixture = None

    def setUp(self):
        if not REPRO or not os.path.exists(REPRO):
            self.skipTest("REPRO_BIN not set (run via meson test)")
        self.sb = Sandbox(self.fixture)
        self.addCleanup(self.sb.cleanup)

    def assertOk(self, r, msg=None):
        self.assertEqual(r.code, 0, msg or repr(r))

    def assertExit(self, r, code):
        self.assertEqual(r.code, code, repr(r))

    def assertError(self, r, code, exit_code=2):
        """Error contract (SC-007): `error[E_CODE]: ...` + a `hint:` line."""
        self.assertEqual(r.code, exit_code, repr(r))
        self.assertRegex(r.err, rf"error\[{code}\]: .+", repr(r))
        self.assertRegex(r.err, r"\n  hint: .+", repr(r))


def journal_steps(text):
    """Parses `repro journal` step lines into (status, step_id) tuples."""
    out = []
    for line in text.splitlines():
        m = re.match(r"^\s+\d+\s+(ok|skip|FAIL|interrupted|blocked)\s+(\S+)", line)
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def main():
    unittest.main(verbosity=2)
