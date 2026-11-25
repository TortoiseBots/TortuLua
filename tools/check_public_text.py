#!/usr/bin/env python3
"""Fail when private or internal details reach this public repository.

Scans every tracked text file except ``dep/lua/`` (vendored, unchanged) and the
commit messages in ``BASE..HEAD`` for:

* IPv4 addresses outside the documentation ranges ``192.0.2.0/24``,
  ``198.51.100.0/24`` and ``203.0.113.0/24`` and the two loopback/unspecified
  literals ``127.0.0.1`` and ``0.0.0.0``;
* home directories, ``/home/<name>`` and ``C:\\Users\\<name>``;
* the regular expressions in ``tools/public_text_denylist.txt``;
* password literals: ``password`` used as a key with ``=`` or ``:``, quoted or
  not, followed by a value of six or more characters.

The denylist file is scanned for the address, home-directory and password
checks but not for its own expressions, which would match themselves. The
allow-list is not scanned at all: every non-comment line is a literal exception
and would match itself. Everything else is fair game, including this script.

False positives are silenced in ``tools/public_text_allow.txt``, one
``path:line-substring`` per line: the substring must occur literally on the
reported line of that exact path, and only the finding whose match overlaps
that occurrence is suppressed -- a second leak on the same line still fails.
Entries shorter than four characters are rejected as too broad. Allow-list
entries never apply to commit messages; those must be clean.

Exit status: 0 clean, 1 findings, 2 usage or git error.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
DENYLIST_PATH = TOOLS_DIR / "public_text_denylist.txt"
ALLOWLIST_PATH = TOOLS_DIR / "public_text_allow.txt"

# Vendored upstream sources: kept byte-identical to the release, not our text.
SKIP_PREFIXES = ("dep/lua/",)
DENYLIST_REL = "tools/public_text_denylist.txt"
ALLOWLIST_REL = "tools/public_text_allow.txt"
# Literal exception text matches itself; the file is validated when loaded.
SKIP_FILES = (ALLOWLIST_REL,)
ALLOWLIST_MIN_LENGTH = 4

IPV4_RE = re.compile(r"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])")
DOC_IPV4_PREFIXES = ("192.0.2.", "198.51.100.", "203.0.113.")
ALLOWED_IPV4 = ("127.0.0.1", "0.0.0.0")

HOME_POSIX_RE = re.compile(r"(?<![\w.])/home/[A-Za-z0-9._-]+(?![A-Za-z0-9._-])")
HOME_WINDOWS_RE = re.compile(r"(?i)(?<![A-Za-z0-9])[A-Za-z]:\\+Users\\+[^\\\s\"'<>]+")
PASSWORD_RE = re.compile(
    r"""(?ix)
    (?: ["']? password ["']? \s* [:=] \s* )
    (?: "[^"\n]{6,}" | '[^'\n]{6,}' | [^\s"'#,;]{6,} )
    """
)
BINARY_PROBE = 8192


@dataclass(frozen=True)
class Hit:
    path: str
    line: int
    text: str
    kind: str
    matched: str
    start: int
    end: int

    @property
    def label(self) -> str:
        return f"{self.path}:{self.line}"


def hit_at(rel: str, lineno: int, line: str, kind: str, match: re.Match[str]) -> Hit:
    start, end = match.span()
    return Hit(rel, lineno, line, kind, match.group(0), start, end)


def die(message: str) -> "NoReturn":
    sys.stderr.write(f"check_public_text: {message}\n")
    raise SystemExit(2)


def git(*args: str) -> bytes:
    result = subprocess.run(
        ["git", *args], cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode("utf-8", "replace"))
        die(f"git {' '.join(args)} failed")
    return result.stdout


def load_denylist(path: Path) -> list[tuple[int, str, re.Pattern[str]]]:
    if not path.is_file():
        die(f"missing {path.relative_to(REPO_ROOT)}")
    patterns: list[tuple[int, str, re.Pattern[str]]] = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        try:
            patterns.append((lineno, entry, re.compile(entry, re.IGNORECASE)))
        except re.error as exc:
            die(f"{path.name}:{lineno}: bad regular expression: {exc}")
    return patterns


def load_allowlist(path: Path) -> dict[str, list[str]]:
    allow: dict[str, list[str]] = {}
    if not path.is_file():
        return allow
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        rel, sep, substring = entry.partition(":")
        if not sep or not rel or not substring:
            die(
                f"{path.name}:{lineno}: expected 'path:line-substring', got {entry!r}"
            )
        if len(substring) < ALLOWLIST_MIN_LENGTH:
            die(
                f"{path.name}:{lineno}: exception {substring!r} is shorter than "
                f"{ALLOWLIST_MIN_LENGTH} characters and would hide unrelated findings"
            )
        allow.setdefault(rel, []).append(substring)
    return allow


def allowed(hit: Hit, allow: dict[str, list[str]]) -> bool:
    """True when an allow-list substring on this path overlaps this finding."""
    for substring in allow.get(hit.path, ()):
        start = hit.text.find(substring)
        while start != -1:
            if start < hit.end and hit.start < start + len(substring):
                return True
            start = hit.text.find(substring, start + 1)
    return False


def tracked_files() -> list[str]:
    return [
        os.fsdecode(name)
        for name in git("ls-files", "-z").split(b"\0")
        if name
    ]


def scan_text(rel: str, text: str, denylist: list[tuple[int, str, re.Pattern[str]]]) -> list[Hit]:
    hits: list[Hit] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for match in IPV4_RE.finditer(line):
            address = match.group(0)
            if any(int(octet) > 255 for octet in address.split(".")):
                continue
            if address in ALLOWED_IPV4:
                continue
            if address.startswith(DOC_IPV4_PREFIXES):
                continue
            hits.append(hit_at(rel, lineno, line, "ipv4 address", match))
        for match in HOME_POSIX_RE.finditer(line):
            hits.append(hit_at(rel, lineno, line, "home directory", match))
        for match in HOME_WINDOWS_RE.finditer(line):
            hits.append(hit_at(rel, lineno, line, "home directory", match))
        for match in PASSWORD_RE.finditer(line):
            hits.append(hit_at(rel, lineno, line, "password literal", match))
        for _entry_line, entry, pattern in denylist:
            for match in pattern.finditer(line):
                hits.append(hit_at(rel, lineno, line, f"denylisted /{entry}/", match))
    return hits


def scan_files(denylist: list[tuple[int, str, re.Pattern[str]]]) -> tuple[list[Hit], int]:
    hits: list[Hit] = []
    scanned = 0
    for rel in tracked_files():
        if rel in SKIP_FILES or rel.startswith(SKIP_PREFIXES):
            continue
        abs_path = REPO_ROOT / rel
        try:
            data = abs_path.read_bytes()
        except OSError:
            continue
        if b"\0" in data[:BINARY_PROBE]:
            continue
        scanned += 1
        # The denylist must spell out the forbidden words, so its own
        # expressions are not applied to it; the other checks still are.
        patterns = [] if rel == DENYLIST_REL else denylist
        hits.extend(scan_text(rel, data.decode("utf-8", "replace"), patterns))
    return hits, scanned


def scan_commits(
    base: str | None, head: str, denylist: list[tuple[int, str, re.Pattern[str]]]
) -> tuple[list[Hit], int]:
    if base:
        git("rev-parse", "--verify", f"{base}^{{commit}}")
        revision_range = [f"{base}..{head}"]
    else:
        revision_range = ["--max-count=1", head]
    raw = git("log", "--format=%H%x1f%B%x1e", *revision_range)
    hits: list[Hit] = []
    commits = 0
    for record in raw.split(b"\x1e"):
        record = record.strip(b"\n")
        if not record:
            continue
        sha, _, body = record.partition(b"\x1f")
        short = sha.decode("ascii")[:12]
        commits += 1
        for lineno, line in enumerate(body.decode("utf-8", "replace").splitlines(), 1):
            for hit in scan_text(f"commit {short}", line, denylist):
                hits.append(hit)
    return hits, commits


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--base",
        help="first commit NOT to scan for commit messages (the PR base sha in CI); "
        "without it only HEAD's message is checked",
    )
    parser.add_argument("--head", default="HEAD", help="last commit to scan (default: HEAD)")
    parser.add_argument(
        "--no-commits", action="store_true", help="scan the tree only (local use)"
    )
    args = parser.parse_args(argv)

    denylist = load_denylist(DENYLIST_PATH)
    allow = load_allowlist(ALLOWLIST_PATH)

    tree_hits, files = scan_files(denylist)
    kept = [hit for hit in tree_hits if not allowed(hit, allow)]

    commits = 0
    if not args.no_commits:
        commit_hits, commits = scan_commits(args.base, args.head, denylist)
        kept.extend(commit_hits)

    if not kept:
        print(f"public-text check: OK ({files} files, {commits} commit(s))")
        return 0

    print(f"public-text check: FAIL, {len(kept)} finding(s) in {files} file(s)")
    for hit in kept:
        print(f"  {hit.label}: {hit.kind}: {hit.matched}")
        print(f"    {hit.text.strip()[:240]}")
        if not hit.path.startswith("commit "):
            print(
                "    if this is a false positive, add "
                f"'{hit.path}:{hit.matched}' to tools/public_text_allow.txt"
            )
    print(
        "commit messages cannot be allow-listed: reword or rebase them out of the range"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
