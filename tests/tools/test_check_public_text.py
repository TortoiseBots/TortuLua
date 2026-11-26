#!/usr/bin/env python3
"""Tests for tools/check_public_text.py.

Each test builds a throwaway git repository in a temporary directory, copies the
checker into its tools/ folder and runs it there, so the real repository and its
history are never touched. Planted leaks are assembled at run time (never
written out whole), so this file passes the check it tests.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "check_public_text.py"
REAL_ROOT = SCRIPT.parents[1]

# Planted values, split so this source file itself stays clean.
PRIVATE_IP = ".".join(["10", "20", "30", "40"])
SECOND_IP = ".".join(["172", "16", "5", "9"])
DOC_IPS = ["192.0.2.7", "198.51.100.23", "203.0.113.250"]
LOOPBACK_IPS = ["127.0.0.1", "0.0.0.0"]
POSIX_HOME = "/" + "home/alice/tortulua-notes.txt"
MACOS_HOME = "/" + "Users/alice/tortulua-notes.txt"
ROOT_PATH = "/" + "root/tortulua-notes.txt"
ROOT_DIR_ONLY = "/" + "root/"
WINDOWS_HOME = "C:" + chr(92) + "Users" + chr(92) + "alice"
WINDOWS_FORWARD_HOME = "C:" + "/Users/" + "alice"
NOREPLY_EMAIL = "12345678+" + "test-user@users.noreply.github.com"
PRIVATE_EMAIL = "alice" + "@example.invalid"
SECRET_WORD = "secret" + "word"
PASSWORD_LINE = "password" + " = " + "hunt" + "er2!"
YAML_PASSWORD_LINE = "password" + ": " + "hunt" + "er2!"
JSON_PASSWORD_LINE = '{"pass' + 'word": "hunt' + 'er2"}'

# The shipped denylist must keep these generic shapes; assembled here so this
# file does not spell out the forbidden words itself.
REQUIRED_DENYLIST = [
    "-au" + "dit\\b",
    "forever-" + "test[0-9]",
    "tortoise-" + "test[0-9]",
    "\\bvm" + "host\\b",
    "\\byu" + "lon\\b",
    "\\bcla" + "ude\\b",
    "\\bco" + "dex\\b",
    "\\bdeep" + "seek\\b",
    "\\bgem" + "ini\\b",
    "\\bo" + "pus\\b",
    "\\bson" + "net\\b",
    "\\bhai" + "ku\\b",
    "\\bmuse-" + "spark\\b",
]


class CheckerRepoTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="public-text-")
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        tools = self.root / "tools"
        tools.mkdir()
        shutil.copy2(SCRIPT, tools / "check_public_text.py")
        self.path = tools / "check_public_text.py"
        self.write_file("tools/public_text_denylist.txt", "\\b" + SECRET_WORD + "\\b\n")
        self.write_file("tools/public_text_allow.txt", "# empty\n")
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", NOREPLY_EMAIL)

    def write_file(self, rel: str, content: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def allow(self, *entries: str) -> None:
        self.write_file("tools/public_text_allow.txt", "\n".join(entries) + "\n")

    def git(self, *args: str) -> str:
        result = subprocess.run(
            ["git", *args], cwd=self.root, check=True, capture_output=True, text=True
        )
        return result.stdout.strip()

    def commit(self, message: str, email: str | None = None) -> str:
        self.git("add", "-A")
        args = ["commit", "-q", "--allow-empty", "-m", message]
        if email is not None:
            self.git("-c", f"user.email={email}", *args)
        else:
            self.git(*args)
        return self.git("rev-parse", "HEAD")

    def run_checker(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.path), *args],
            cwd=self.root,
            capture_output=True,
            text=True,
        )

    # --- clean trees -----------------------------------------------------

    def test_clean_tree_passes(self) -> None:
        self.write_file("README.md", "A Lua host for test servers.\n")
        self.write_file("docs/notes.md", "Loopback " + LOOPBACK_IPS[0] + " only.\n")
        self.commit("Initial import")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("OK", result.stdout)

    def test_documentation_ipv4_ranges_pass(self) -> None:
        for index, address in enumerate(DOC_IPS):
            self.write_file(f"docs/example{index}.md", "addr " + address + "\n")
        self.write_file("docs/loopback.md", " ".join(LOOPBACK_IPS) + "\n")
        self.commit("Documentation examples only")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_binary_and_vendored_files_are_skipped(self) -> None:
        self.write_file("assets/logo.bin", "x" + PRIVATE_IP + "\x00\x01")
        self.write_file("dep/lua/lstate.c", "static const char *host = \"" + PRIVATE_IP + "\";\n")
        self.commit("Vendor a blob and the upstream tree")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    # --- planted leaks ---------------------------------------------------

    def test_planted_ipv4_fails(self) -> None:
        self.write_file("docs/deploy.md", "Server: " + PRIVATE_IP + "\n")
        self.commit("Document the server")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("docs/deploy.md:1", result.stdout)
        self.assertIn("ipv4 address", result.stdout)
        self.assertIn(PRIVATE_IP, result.stdout)
        self.assertIn(
            "docs/deploy.md:" + PRIVATE_IP + "' to tools/public_text_allow.txt",
            result.stdout,
        )

    def test_home_directories_fail(self) -> None:
        self.write_file("docs/posix.md", "Copied from " + POSIX_HOME + "\n")
        self.write_file("docs/windows.md", "Log at " + WINDOWS_HOME + "\n")
        self.commit("Document local paths")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("docs/posix.md:1", result.stdout)
        self.assertIn("docs/windows.md:1", result.stdout)
        self.assertEqual(result.stdout.count("home directory"), 2, result.stdout)

    def test_windows_forward_slash_home_directory_fails(self) -> None:
        self.write_file("docs/win-fwd.md", "Log at " + WINDOWS_FORWARD_HOME + "/notes.txt\n")
        self.commit("Document local path")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("docs/win-fwd.md:1", result.stdout)
        self.assertIn("home directory", result.stdout)
        self.assertIn(WINDOWS_FORWARD_HOME, result.stdout)

    def test_macos_home_directory_fails(self) -> None:
        self.write_file("docs/macos.md", "Copied from " + MACOS_HOME + "\n")
        self.commit("Document local path")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("docs/macos.md:1", result.stdout)
        self.assertIn("home directory", result.stdout)

    def test_root_paths_fail(self) -> None:
        self.write_file("docs/root.md", "Copied from " + ROOT_PATH + "\nDir " + ROOT_DIR_ONLY + "\n")
        self.write_file("docs/clean.md", "Relative src" + ROOT_PATH + "\n")
        self.commit("Document local path")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("docs/root.md:1", result.stdout)
        self.assertIn("docs/root.md:2", result.stdout)
        self.assertNotIn("docs/clean.md", result.stdout)
        self.assertEqual(result.stdout.count("home directory"), 2, result.stdout)

    def test_denylisted_word_fails(self) -> None:
        self.write_file("docs/credits.md", "Thanks to " + SECRET_WORD + ".\n")
        self.commit("Credit the author")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("denylisted", result.stdout)
        self.assertIn(SECRET_WORD, result.stdout)

    def test_password_literal_fails(self) -> None:
        self.write_file("conf/sample.conf", "db " + PASSWORD_LINE + "\n")
        self.commit("Add a sample configuration")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("password literal", result.stdout)

    def test_password_as_yaml_and_json_key_fails(self) -> None:
        self.write_file("conf/sample.yaml", YAML_PASSWORD_LINE + "\n")
        self.write_file("conf/sample.json", JSON_PASSWORD_LINE + "\n")
        self.commit("Add sample configuration")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("conf/sample.yaml:1", result.stdout)
        self.assertIn("conf/sample.json:1", result.stdout)
        self.assertEqual(result.stdout.count("password literal"), 2, result.stdout)

    def test_allowlisted_line_passes(self) -> None:
        self.write_file("docs/deploy.md", "Server: " + PRIVATE_IP + "\n")
        self.write_file("docs/other.md", "Server: " + SECOND_IP + "\n")
        self.allow("docs/deploy.md:" + PRIVATE_IP)
        self.commit("Document the server")
        result = self.run_checker()
        # The allow-listed line is gone, the unlisted one still fails.
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertNotIn(PRIVATE_IP, result.stdout)
        self.assertIn(SECOND_IP, result.stdout)

    def test_second_leak_on_allowlisted_line_fails(self) -> None:
        self.write_file(
            "docs/deploy.md", "host " + PRIVATE_IP + " " + PASSWORD_LINE + "\n"
        )
        self.allow("docs/deploy.md:" + PRIVATE_IP)
        self.commit("Document the server")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertNotIn("ipv4 address", result.stdout)
        self.assertIn("password literal", result.stdout)

    def test_short_allowlist_entry_is_rejected(self) -> None:
        self.write_file("docs/deploy.md", "host " + PRIVATE_IP + "\n")
        self.allow("docs/deploy.md:10.")
        self.commit("Document the server")
        result = self.run_checker()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("shorter than", result.stderr)

    def test_denylist_is_scanned_for_addresses(self) -> None:
        # Its own expressions are exempt, anything else on its lines is not.
        self.write_file(
            "tools/public_text_denylist.txt",
            "\\b" + SECRET_WORD + "\\b\n" + PRIVATE_IP + "\n",
        )
        self.commit("Widen the denylist")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("tools/public_text_denylist.txt:2", result.stdout)
        self.assertNotIn("tools/public_text_denylist.txt:1: ", result.stdout)

    def test_allowlist_is_path_scoped(self) -> None:
        self.write_file("docs/deploy.md", "Server: " + PRIVATE_IP + "\n")
        self.write_file("docs/other.md", "Server: " + PRIVATE_IP + "\n")
        self.allow("docs/deploy.md:" + PRIVATE_IP)
        self.commit("Document the server")
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("docs/other.md:1", result.stdout)
        self.assertNotIn("docs/deploy.md:1", result.stdout)

    # --- commit messages -------------------------------------------------

    def test_commit_message_hit_fails(self) -> None:
        base = self.commit("Initial import")
        self.write_file("README.md", "No secrets here.\n")
        self.commit("Retry against " + PRIVATE_IP)
        result = self.run_checker("--base", base)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("commit ", result.stdout)
        self.assertIn(PRIVATE_IP, result.stdout)
        self.assertIn("cannot be allow-listed", result.stdout)

    def test_commit_before_base_is_ignored(self) -> None:
        self.commit("Retry against " + PRIVATE_IP)
        head = self.commit("Clean follow-up")
        result = self.run_checker("--base", head)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 commit(s)", result.stdout)

    def test_commit_author_email_must_be_github_noreply(self) -> None:
        base = self.commit("Initial import")
        self.write_file("README.md", "No secrets here.\n")
        self.commit("Clean message", email=PRIVATE_EMAIL)
        result = self.run_checker("--base", base)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("commit ", result.stdout)
        self.assertIn("author email", result.stdout)
        self.assertIn(PRIVATE_EMAIL, result.stdout)
        self.assertIn("cannot be allow-listed", result.stdout)

    def test_commit_author_email_before_base_is_ignored(self) -> None:
        self.commit("Earlier commit", email=PRIVATE_EMAIL)
        head = self.commit("Clean follow-up")
        result = self.run_checker("--base", head)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0 commit(s)", result.stdout)

    # --- the real repository ---------------------------------------------

    def test_repository_itself_is_clean(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--no-commits"],
            cwd=REAL_ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class ShippedPolicyTest(unittest.TestCase):
    def test_denylist_keeps_the_required_seeds(self) -> None:
        text = (REAL_ROOT / "tools" / "public_text_denylist.txt").read_text(encoding="utf-8")
        entries = [
            line.strip()
            for line in text.splitlines()
            if line.strip() and not line.strip().startswith("#")
        ]
        for seed in REQUIRED_DENYLIST:
            self.assertIn(seed, entries)
        for entry in entries:
            re.compile(entry)
            self.assertGreater(len(entry), 3, entry)

    def test_allowlist_entries_are_well_formed(self) -> None:
        text = (REAL_ROOT / "tools" / "public_text_allow.txt").read_text(encoding="utf-8")
        for line in text.splitlines():
            entry = line.strip()
            if not entry or entry.startswith("#"):
                continue
            path, sep, substring = entry.partition(":")
            self.assertTrue(sep and path and substring, entry)
            self.assertGreaterEqual(len(substring), 4, entry)


if __name__ == "__main__":
    unittest.main()
