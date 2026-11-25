#!/usr/bin/env python3
"""Generator for LuaLS / sumneko editor type hints (types/).

Reads docs/coverage/coverage.json and docs/coverage/eluna-documented-api.json (and C++ engine sources when available)
and emits definition files in types/ for the Lua language server:
  - types/globals.lua
  - types/<Class>.lua for each class
  - types/events.lua with event id constants per table
  - types/README.md with editor configuration (.luarc.json example)

Usage:
    tools/typehints/gen_typehints.py                # write or update types/
    tools/typehints/gen_typehints.py --check        # exit 1 when types/ is stale or missing
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS_DIR = ROOT / "docs" / "coverage"
REPORT_JSON = DOCS_DIR / "coverage.json"
DOCS_JSON = DOCS_DIR / "eluna-documented-api.json"
TYPES_DIR = ROOT / "types"
SRC_ENGINE_DIR = ROOT / "src" / "engine"

FAMILY_TABLES = {
    "server": "ServerEvents",
    "player": "PlayerEvents",
    "creature": "CreatureEvents",
    "gameobject": "GameObjectEvents",
    "item": "ItemEvents",
    "player gossip": "PlayerGossipEvents",
    "creature gossip": "CreatureGossipEvents",
    "gameobject gossip": "GameObjectGossipEvents",
    "item gossip": "ItemGossipEvents",
    "group": "GroupEvents",
    "guild": "GuildEvents",
    "map": "MapEvents",
    "instance": "InstanceEvents",
    "packet": "PacketEvents",
    "battleground": "BattleGroundEvents",
    "spell": "SpellEvents",
    "bot": "BotEvents",
}

KNOWN_GLOBAL_PARAMS = {
    "print": ["..."],
    "require": ["modname"],
    "RegisterCommand": ["command", "security", "handler"],
    "ForeverData": {
        "get": ["key"],
        "set": ["key", "value"],
        "delete": ["key"],
        "keys": [],
    },
}


def strip_cpp_comments(text: str) -> str:
    """Removes single-line and multi-line comments from C++ source text."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i : i + 2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                break
            out.append(" ")
            i = end + 2
        elif text[i : i + 2] == "//":
            end = text.find("\n", i + 2)
            if end == -1:
                break
            out.append("\n")
            i = end + 1
        elif text[i] == '"':
            # string literal
            out.append('"')
            i += 1
            while i < n:
                ch = text[i]
                out.append(ch)
                if ch == "\\":
                    i += 1
                    if i < n:
                        out.append(text[i])
                elif ch == '"':
                    break
                i += 1
            i += 1
        elif text[i] == "'":
            # char literal
            out.append("'")
            i += 1
            while i < n:
                ch = text[i]
                out.append(ch)
                if ch == "\\":
                    i += 1
                    if i < n:
                        out.append(text[i])
                elif ch == "'":
                    break
                i += 1
            i += 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def find_matching_brace(text: str, start: int, open_ch="{", close_ch="}") -> int:
    depth = 0
    for i in range(start, len(text)):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
    return len(text)


def scan_functions(text: str) -> dict:
    """Extract top-level and namespace function bodies from C++ text."""
    fns = {}
    pat = re.compile(r"(?:^|\n)(?:static\s+|inline\s+|constexpr\s+)?(?:[\w:<>&*]+)\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const|noexcept)?\s*\{")
    pos = 0
    while True:
        m = pat.search(text, pos)
        if not m:
            break
        fn_name = m.group(1)
        open_pos = m.end() - 1
        close_pos = find_matching_brace(text, open_pos, "{", "}")
        body = text[open_pos + 1 : close_pos]
        fns.setdefault(fn_name, body)
        pos = close_pos + 1
    return fns


class SourceScanner:
    """Extracts function bodies and parameter names/counts from engine C++ files."""

    def __init__(self, src_engine_dir: pathlib.Path = SRC_ENGINE_DIR):
        self.src_dir = src_engine_dir
        self.fns = {}
        self.methods = {}   # cls -> {method_name: fn_name}
        self.globals = {}   # global_name -> fn_name
        if not self.src_dir.exists():
            return
        elu_dir = self.src_dir / "elu"
        files_to_scan = []
        if elu_dir.exists():
            files_to_scan.extend(sorted(elu_dir.glob("*.cpp")))
        for extra in ["EluAlias.cpp", "Api.cpp", "Engine.cpp"]:
            p = self.src_dir / extra
            if p.exists():
                files_to_scan.append(p)

        for path in files_to_scan:
            try:
                raw = path.read_text(encoding="utf-8")
                clean = strip_cpp_comments(raw)
                fns = scan_functions(clean)
                for name, body in fns.items():
                    self.fns.setdefault(name, body)
            except Exception:
                pass

        self._scan_registrations()

    def _scan_registrations(self):
        # Extract addMethods(L, "ClassName", TableName) and luaL_Reg TableName[]
        for fn_name, body in self.fns.items():
            tables = {}
            for tm in re.finditer(r"(?:static\s+)?luaL_Reg\s+const\s+(\w+)\s*\[\]\s*=\s*\{", body):
                end = find_matching_brace(body, tm.end() - 1, "{", "}")
                tname = tm.group(1)
                pairs = re.findall(r'\{\s*"(\w+)"\s*,\s*(?:fl::)?safe<(?:\w+::)?(\w+)>', body[tm.end() : end])
                tables[tname] = pairs

            for am in re.finditer(r"addMethods\(L,\s*\"(\w+)\",\s*(\w+)\)", body):
                cls, tbl = am.group(1), am.group(2)
                if tbl in tables:
                    self.methods.setdefault(cls, {})
                    for method, f in tables[tbl]:
                        self.methods[cls][method] = f

            for m in re.finditer(r"set\(L,\s*\"(\w+)\",\s*(?:fl::)?safe<(?:\w+::)?(\w+)>\)", body):
                self.globals[m.group(1)] = m.group(2)

    def extract_params(self, fn_name: str, is_method: bool = True, max_depth: int = 2) -> list:
        """Derive parameter names/counts from function body and single forwards."""
        if fn_name not in self.fns:
            return []
        body = self.fns[fn_name]
        start_idx = 2 if is_method else 1

        params = {}
        max_idx = start_idx - 1

        # Check for named arg calls: argStr(L, 2, "message")
        for m in re.finditer(r"\b(?:arg|opt)\w*\s*\(\s*L\s*,\s*(\d+)\s*,\s*\"([^\"]+)\"", body):
            idx = int(m.group(1))
            if idx >= start_idx:
                params[idx] = m.group(2)
                if idx > max_idx:
                    max_idx = idx

        # Check for opt calls with 4 args: optStr(L, 2, "def", "name")
        for m in re.finditer(r"\bopt\w*\s*\(\s*L\s*,\s*(\d+)\s*,\s*[^,)]+\s*,\s*\"([^\"]+)\"", body):
            idx = int(m.group(1))
            if idx >= start_idx:
                params[idx] = m.group(2)
                if idx > max_idx:
                    max_idx = idx

        # Any other helper that takes the Lua state and a stack index: objArg(L, 2, kUnit, "other"),
        # unitStateArg(L, 2), cell(L, 2), itemOrEntry(L, 2). A quoted identifier in the same call names the parameter.
        for m in re.finditer(r"\b(\w+)\s*\(\s*L\s*,\s*(\d+)\b([^;()]*)", body):
            if m.group(1).startswith(("lua_push", "luaL_error", "lua_settop", "lua_pop", "lua_rawgeti")):
                continue
            idx = int(m.group(2))
            if start_idx <= idx <= 12:  # a larger number is a bit width or count, not a stack index
                if idx > max_idx:
                    max_idx = idx
                nm = re.search(r'"([A-Za-z_]\w*)"', m.group(3))
                if nm and idx not in params:
                    params[idx] = nm.group(1)

        # If nothing found and body forwards to another function, follow forward
        if max_idx < start_idx and max_depth > 0:
            fm = re.search(r"\breturn\s+([A-Za-z_]\w*)\s*\(", body)
            if fm:
                target = fm.group(1)
                if target in self.fns:
                    # In forwarded helper (like registerNoEntry(L, kind)), indices for globals start at 1
                    # while for methods forwarded, depends on whether self was passed
                    f_params = self.extract_params(target, is_method=is_method, max_depth=max_depth - 1)
                    if f_params:
                        return f_params

        if max_idx < start_idx:
            return []

        res = []
        for i in range(start_idx, max_idx + 1):
            name = params.get(i)
            if not name:
                name = f"arg{i - start_idx + 1}"
            name = re.sub(r"\W", "_", name)
            if not name or name[0].isdigit():
                name = f"arg{i - start_idx + 1}"
            res.append(name)
        return res


def sanitize_stub_note(note: str | None) -> str:
    if not note:
        return "not available on Classic 1.60"
    if note.startswith("stub: "):
        note = note[len("stub: ") :].strip()
    return note


def get_params_for_method(cls: str, method: str, minfo: dict, doc_entry: any, scanner: SourceScanner | None) -> list:
    # 1. Check doc_entry if it provides parameter names
    if isinstance(doc_entry, list) and doc_entry and not all(x in ("docs", "spec") for x in doc_entry):
        return [str(p) for p in doc_entry]
    if isinstance(doc_entry, dict):
        for k in ["params", "args", "parameters"]:
            if k in doc_entry and isinstance(doc_entry[k], list):
                return [str(p) for p in doc_entry[k]]

    # 2. Derive from scanner if implemented
    if scanner and minfo.get("status") == "implemented":
        fn_name = scanner.methods.get(cls, {}).get(method)
        if fn_name:
            p = scanner.extract_params(fn_name, is_method=True)
            if p:
                return p
            if cls == "ElunaQuery" and (method.startswith("Get") or method == "IsNull"):
                return ["column"]  # the shared pushNumeric helper takes the column at stack index 2

    return []


def get_params_for_global(name: str, ginfo: dict, doc_entry: any, scanner: SourceScanner | None) -> list:
    if name in KNOWN_GLOBAL_PARAMS and isinstance(KNOWN_GLOBAL_PARAMS[name], list):
        return KNOWN_GLOBAL_PARAMS[name]

    # 1. Check doc_entry if it provides parameter names
    if isinstance(doc_entry, list) and doc_entry and not all(x in ("docs", "spec") for x in doc_entry):
        return [str(p) for p in doc_entry]
    if isinstance(doc_entry, dict):
        for k in ["params", "args", "parameters"]:
            if k in doc_entry and isinstance(doc_entry[k], list):
                return [str(p) for p in doc_entry[k]]

    # 2. Derive from scanner if implemented
    if scanner and ginfo.get("status") == "implemented":
        fn_name = scanner.globals.get(name)
        if fn_name:
            p = scanner.extract_params(fn_name, is_method=False)
            if p:
                return p

    return []


def generate_class_lua(cls: str, inherits: str | None, methods_dict: dict, doc_methods: dict, scanner: SourceScanner | None, is_extension: bool = False, ext_notes: dict | None = None) -> tuple[str, dict]:
    """Generates the contents of types/<Class>.lua."""
    lines = ["---@meta", ""]
    if inherits:
        lines.append(f"---@class {cls} : {inherits}")
    else:
        lines.append(f"---@class {cls}")
    lines.append(f"local {cls} = {{}}")

    stats = {"implemented": 0, "stub": 0, "extension": 0}

    # Sort methods alphabetically
    all_methods = sorted(methods_dict.keys(), key=str.lower)

    for method in all_methods:
        minfo = methods_dict[method]
        status = minfo.get("status") if isinstance(minfo, dict) else "implemented"
        declared = minfo.get("declared", cls) if isinstance(minfo, dict) else cls

        # If declared on parent class, skip (inherited via ---@class Child : Parent)
        if declared != cls:
            continue

        if status == "missing":
            continue

        lines.append("")

        if is_extension:
            note = ext_notes.get(method) if ext_notes else None
            if note:
                lines.append(f"---{note}")
            else:
                lines.append("---our extension")
            params = get_params_for_method(cls, method, {}, doc_methods.get(method), scanner)
            for p in params:
                lines.append(f"---@param {p} any")
            lines.append("---@return any")
            sig = ", ".join(params)
            lines.append(f"function {cls}:{method}({sig}) end")
            stats["extension"] += 1
        elif status == "stub":
            note = minfo.get("note") if isinstance(minfo, dict) else None
            msg = sanitize_stub_note(note)
            lines.append(f"---@deprecated {msg}")
            lines.append("---@return any")
            lines.append(f"function {cls}:{method}() end")
            stats["stub"] += 1
        elif status == "implemented":
            note = minfo.get("note") if isinstance(minfo, dict) else None
            if note:
                lines.append(f"---{note}")
            params = get_params_for_method(cls, method, minfo if isinstance(minfo, dict) else {}, doc_methods.get(method), scanner)
            for p in params:
                lines.append(f"---@param {p} any")
            lines.append("---@return any")
            sig = ", ".join(params)
            lines.append(f"function {cls}:{method}({sig}) end")
            stats["implemented"] += 1

    lines.append("")
    return "\n".join(lines), stats


def generate_globals_lua(cov_globals: dict, extra_globals: dict, doc_globals: dict, scanner: SourceScanner | None) -> tuple[str, dict]:
    """Generates types/globals.lua."""
    lines = ["---@meta", ""]
    stats = {"implemented": 0, "stub": 0, "extension": 0}

    # 1. Documented globals from coverage
    for name in sorted(cov_globals.keys(), key=str.lower):
        ginfo = cov_globals[name]
        status = ginfo.get("status")
        if status == "missing":
            continue

        lines.append("")
        if status == "stub":
            note = ginfo.get("note")
            msg = sanitize_stub_note(note)
            lines.append(f"---@deprecated {msg}")
            lines.append("---@return any")
            lines.append(f"function {name}() end")
            stats["stub"] += 1
        elif status == "implemented":
            note = ginfo.get("note")
            if note:
                lines.append(f"---{note}")
            params = get_params_for_global(name, ginfo, doc_globals.get(name), scanner)
            for p in params:
                lines.append(f"---@param {p} any")
            lines.append("---@return any")
            sig = ", ".join(params)
            lines.append(f"function {name}({sig}) end")
            stats["implemented"] += 1

    # 2. Extensions (from extra["globals"])
    lines.append("\n-- Engine extensions")
    for name in sorted(extra_globals.keys(), key=str.lower):
        einfo = extra_globals[name]
        note = einfo.get("note") or "our extension"
        table_subs = einfo.get("table")

        lines.append("")
        if table_subs:
            # Global table with functions, like ForeverData
            lines.append(f"---{note}")
            lines.append(f"---@class {name}")
            lines.append(f"{name} = {{}}")
            for sub in table_subs:
                lines.append("")
                lines.append(f"---{note}")
                sub_params = KNOWN_GLOBAL_PARAMS.get(name, {}).get(sub, [])
                for sp in sub_params:
                    lines.append(f"---@param {sp} any")
                lines.append("---@return any")
                sub_sig = ", ".join(sub_params)
                lines.append(f"function {name}.{sub}({sub_sig}) end")
            stats["extension"] += 1
        else:
            lines.append(f"---{note}")
            params = get_params_for_global(name, {"status": "implemented"}, doc_globals.get(name), scanner)
            for p in params:
                lines.append(f"---@param {p} any")
            lines.append("---@return any")
            sig = ", ".join(params)
            lines.append(f"function {name}({sig}) end")
            stats["extension"] += 1

    lines.append("")
    return "\n".join(lines), stats


def generate_events_lua(cov_events: dict, extra_events: dict) -> tuple[str, dict]:
    """Generates types/events.lua containing event id constant tables."""
    lines = ["---@meta", ""]
    stats = {}

    all_families = list(cov_events.keys())
    for efam in extra_events.keys():
        if efam not in all_families:
            all_families.append(efam)

    for fam in all_families:
        tbl_name = FAMILY_TABLES.get(fam, "".join(w.capitalize() for w in fam.split()) + "Events")
        is_extra = fam in extra_events
        evs = extra_events[fam] if is_extra else cov_events.get(fam, {})

        lines.append(f"---@enum {tbl_name}")
        if is_extra:
            lines.append("---our extension")
        lines.append(f"{tbl_name} = {{")

        count = 0
        # Sort by numeric event id
        def sort_key(item):
            try:
                return (0, int(item[0]))
            except ValueError:
                return (1, str(item[0]))

        for eid_str, item in sorted(evs.items(), key=sort_key):
            if is_extra:
                # extra format: {"1": "BOT_ADDED", ...}
                ename = str(item)
                lines.append(f"    ---our extension")
                lines.append(f"    {ename} = {eid_str},")
                count += 1
            else:
                # cov format: {"1": {"status": "implemented", "name": "...", "note": "...", "requires": "..."}}
                status = item.get("status")
                ename = item.get("name")
                if not ename or status == "missing":
                    continue

                note = item.get("note")
                requires = item.get("requires")

                if status == "stub":
                    smsg = sanitize_stub_note(note)
                    lines.append(f"    ---@deprecated {smsg}")
                elif note:
                    lines.append(f"    ---{note}")
                elif requires:
                    lines.append(f"    ---needs core hook {requires}")

                lines.append(f"    {ename} = {eid_str},")
                count += 1

        lines.append("}")
        if fam == "battleground":
            lines.append(f"BGEvents = {tbl_name}")
        lines.append("")
        stats[fam] = count

    return "\n".join(lines), stats


def generate_readme_md() -> str:
    return """# TortuLua Type Hints

This directory provides type hint definition files for script authors using the [Lua Language Server](https://luals.github.io/) (`LuaLS` / `sumneko.lua`).

## Features

- **Autocomplete & Signatures**: Full autocomplete for all implemented global functions and class methods.
- **Inheritance Hierarchy**: Proper class inheritance (`Player` \u2192 `Unit` \u2192 `WorldObject` \u2192 `Object`, `Creature` \u2192 `Unit`, etc.).
- **Deprecation Warnings**: Classic-era stubs (e.g. methods or events requiring expansions or missing hooks) are marked `---@deprecated`.
- **Clean API Surface**: Missing methods are omitted to prevent scripts from relying on unimplemented calls.
- **Extensions Documented**: Engine extensions (`RegisterCommand`, `ForeverData`, `Bot`, `BotEvents`) are included and annotated.

## Editor Setup (.luarc.json)

To enable type hints in your Lua editor (VS Code, Neovim, etc.), configure your workspace `.luarc.json` to include this `types/` directory in `workspace.library`:

```json
{
  "$schema": "https://raw.githubusercontent.com/sumneko/vscode-lua/master/setting/schema.json",
  "runtime.version": "Lua 5.4",
  "workspace.library": [
    "path/to/TortuLua/types"
  ],
  "diagnostics.globals": [
    "ServerEvents",
    "PlayerEvents",
    "CreatureEvents",
    "GameObjectEvents",
    "ItemEvents",
    "PlayerGossipEvents",
    "CreatureGossipEvents",
    "GameObjectGossipEvents",
    "ItemGossipEvents",
    "GroupEvents",
    "GuildEvents",
    "MapEvents",
    "InstanceEvents",
    "PacketEvents",
    "BattleGroundEvents",
    "BGEvents",
    "SpellEvents",
    "BotEvents",
    "ForeverData"
  ]
}
```

## Regeneration

Type definitions are generated from `docs/coverage/coverage.json` and engine sources:

```bash
python3 tools/typehints/gen_typehints.py          # regenerate types/
python3 tools/typehints/gen_typehints.py --check  # check types/ is up to date
```
"""


def generate_all_types(cov_path: pathlib.Path = REPORT_JSON, doc_path: pathlib.Path = DOCS_JSON, src_dir: pathlib.Path = SRC_ENGINE_DIR) -> tuple[dict[str, str], dict]:
    """Computes all files in types/ in memory.

    Returns:
        (files_dict: {rel_filename: content}, summary_stats: dict)
    """
    if not cov_path.exists():
        raise FileNotFoundError(f"{cov_path} not found")
    cov = json.loads(cov_path.read_text(encoding="utf-8"))

    doc = {}
    if doc_path.exists():
        try:
            doc = json.loads(doc_path.read_text(encoding="utf-8"))
        except Exception:
            pass

    scanner = SourceScanner(src_dir) if src_dir.exists() else None

    files = {}
    summary = {
        "classes": {},
        "globals": {},
        "events": {},
    }

    # 1. Classes
    cov_classes = cov.get("classes", {})
    doc_classes = doc.get("classes", {})

    for cls, cinfo in cov_classes.items():
        inherits = cinfo.get("inherits")
        methods = cinfo.get("methods", {})
        dmethods = doc_classes.get(cls, {})
        fname = f"{cls}.lua"
        content, stats = generate_class_lua(cls, inherits, methods, dmethods, scanner, is_extension=False)
        files[fname] = content
        summary["classes"][cls] = stats

    # Extra classes (e.g. Bot)
    extra_classes = cov.get("extra", {}).get("classes", {})
    for cls, methods in extra_classes.items():
        fname = f"{cls}.lua"
        inherits = None
        dmethods = doc_classes.get(cls, {})
        content, stats = generate_class_lua(cls, inherits, methods, dmethods, scanner, is_extension=True, ext_notes=methods)
        files[fname] = content
        summary["classes"][cls] = stats

    # 2. Globals
    cov_globals = cov.get("globals", {})
    extra_globals = cov.get("extra", {}).get("globals", {})
    doc_globals = doc.get("globals", {})
    g_content, g_stats = generate_globals_lua(cov_globals, extra_globals, doc_globals, scanner)
    files["globals.lua"] = g_content
    summary["globals"] = g_stats

    # 3. Events
    cov_events = cov.get("events", {})
    extra_events = cov.get("extra", {}).get("events", {})
    ev_content, ev_stats = generate_events_lua(cov_events, extra_events)
    files["events.lua"] = ev_content
    summary["events"] = ev_stats

    # 4. README.md
    files["README.md"] = generate_readme_md()

    return files, summary


def check_types(out_dir: pathlib.Path, expected_files: dict[str, str]) -> tuple[bool, list[str]]:
    """Compares expected files against actual directory contents."""
    errors = []
    if not out_dir.exists():
        return False, [f"types directory {out_dir} does not exist"]

    actual_files = set()
    for p in out_dir.iterdir():
        if p.is_file() and not p.name.startswith("."):
            actual_files.add(p.name)

    expected_names = set(expected_files.keys())

    missing = expected_names - actual_files
    if missing:
        errors.append(f"missing files in {out_dir}: {', '.join(sorted(missing))}")

    unexpected = actual_files - expected_names
    if unexpected:
        errors.append(f"unexpected files in {out_dir}: {', '.join(sorted(unexpected))}")

    for name in sorted(expected_names & actual_files):
        actual_content = (out_dir / name).read_text(encoding="utf-8")
        expected_content = expected_files[name]
        if actual_content != expected_content:
            errors.append(f"{out_dir / name} is stale or modified")

    return len(errors) == 0, errors


def write_types(out_dir: pathlib.Path, files: dict[str, str]):
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        p = out_dir / name
        p.write_text(content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Generate LuaLS editor type hints from coverage.json")
    parser.add_argument("--coverage", default=str(REPORT_JSON), help="path to coverage.json")
    parser.add_argument("--doc", default=str(DOCS_JSON), help="path to eluna-documented-api.json")
    parser.add_argument("--src", default=str(SRC_ENGINE_DIR), help="path to src/engine")
    parser.add_argument("--out", default=str(TYPES_DIR), help="output types directory")
    parser.add_argument("--check", action="store_true", help="verify types/ is current; exit 1 if stale")
    args = parser.parse_args()

    cov_path = pathlib.Path(args.coverage)
    doc_path = pathlib.Path(args.doc)
    src_path = pathlib.Path(args.src)
    out_path = pathlib.Path(args.out)

    files, summary = generate_all_types(cov_path, doc_path, src_path)

    if args.check:
        ok, errors = check_types(out_path, files)
        if not ok:
            print("ERROR: type hint definitions are stale or missing:", file=sys.stderr)
            for e in errors:
                print(f"  {e}", file=sys.stderr)
            print("Fix: run `python3 tools/coverage/gen_coverage.py` then `python3 tools/typehints/gen_typehints.py` and commit the changes to docs/ and types/.", file=sys.stderr)
            sys.exit(1)
        print(f"type hints are current ({len(files)} files checked in {out_path})")
        sys.exit(0)

    write_types(out_path, files)
    print(f"wrote {len(files)} type hint definition files to {out_path}")
    for cls, s in summary["classes"].items():
        tot = sum(s.values())
        print(f"  {cls:15}: {tot} methods ({s})")
    print(f"  globals.lua    : {sum(summary['globals'].values())} globals ({summary['globals']})")
    print(f"  events.lua     : {sum(summary['events'].values())} events across {len(summary['events'])} tables")


if __name__ == "__main__":
    main()
