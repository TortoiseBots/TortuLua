#!/usr/bin/env python3
"""Coverage report of the Eluna-compatible surface of this engine (eluna mode).

The truth about the engine is read from the engine itself: the globals the state installs, the method tables
(`luaL_Reg` + `addMethods`, and the section 7 stubs), the event id tables (`SpecRow`) and the per-event hook and
Classic-era notes of `noteEventGaps`. The documented surface is `docs/coverage/eluna-documented-api.json` (built by
fetch_documented_api.py from Eluna's published documentation and the maintainers' API specification, sections 2-4 and 7).

    tools/coverage/gen_coverage.py                     # report to stdout
    tools/coverage/gen_coverage.py --write             # write docs/COVERAGE.md and docs/coverage/coverage.json
    tools/coverage/gen_coverage.py --check             # exit 1 when the checked-in report is stale

`--check` is the CI gate: it regenerates both files in memory, compares them with the checkout, and names every
engine registration the report does not list. No network, no compiler, no core tree.
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ENGINE_DIR = ROOT / "src" / "engine"
ELU_DIR = ENGINE_DIR / "elu"
DOCS_JSON = ROOT / "docs" / "coverage" / "eluna-documented-api.json"
REPORT_MD = ROOT / "docs" / "COVERAGE.md"
REPORT_JSON = ROOT / "docs" / "coverage" / "coverage.json"

# The families of the report, in the brief's order: (report key, documented JSON key, kind).
CLASS_FAMILIES = [
    "Object", "WorldObject", "Unit", "Player", "Creature", "GameObject", "Item", "Group", "Guild", "Quest",
    "Spell", "Aura", "Map", "ElunaQuery", "WorldPacket",
]
OTHER_CLASS_FAMILIES = ["Corpse", "BattleGround", "Vehicle", "Bot", "AuraEffect", "ElunaProcInfo", "SpellInfo"]

EVENT_FAMILIES = [
    "server", "player", "creature", "gameobject", "item", "player gossip", "creature gossip",
    "gameobject gossip", "item gossip", "group", "guild", "map", "instance", "packet", "battleground", "spell",
    "bot",
]

# Engine event family (EluKind) -> report family.
KIND_FAMILY = {
    "Server": "server",
    "Player": "player",
    "Creature": "creature",
    "GameObject": "gameobject",
    "Item": "item",
    "PlayerGossip": "player gossip",
    "CreatureGossip": "creature gossip",
    "GameObjectGossip": "gameobject gossip",
    "ItemGossip": "item gossip",
    "Group": "group",
    "Guild": "guild",
    "Map": "map",
    "Instance": "instance",
    "Packet": "packet",
    "BattleGround": "battleground",
    "Bot": "bot",
}

# Engine names that are not Eluna API: our extensions, the Lua sandbox and the test-mode toolkit.
KNOWN_EXTRA_GLOBALS = {
    "RegisterCommand": "our extension (E3): a GM command slot, README 'Writing scripts'",
    "ForeverData": "our extension (E3): the persistent per-script table",
    "print": "Lua sandbox builtin (Api.cpp registerCommon, both modes)",
    "require": "Lua sandbox module loader (Api.cpp registerCommon, both modes)",
    # Our bot API (stage B1): its own surface, not part of Eluna's.
    "IsBotApiAvailable": "our extension (B1): the bot API is built into this server",
    "GetBots": "our extension (B1): the bot snapshot, in name order",
    "GetBotByName": "our extension (B1): a bot of the snapshot",
    "GetBotByGUID": "our extension (B1): a bot of the snapshot by guid",
    "GetBotsOfOwner": "our extension (B1): the bots of a player's Battle.net account",
    "GetBotsOfMaster": "our extension (B1): the bots a player masters or commands",
    "GetBotsInGroup": "our extension (B1): the bots of a group",
    "RegisterBotEvent": "our extension (B1): the nine bot events, world state only",
    "ClearBotEvents": "our extension (B1): clears the bot handlers",
    # Our bot API (stage B2): the party-level order calls; the per-bot orders are Bot methods.
    "BotsInterrupt": "our extension (B2): the party's interrupt for a unit, one order returned",
    "BotCommanderPlace": "our extension (B2): the chat form of place/rally/fall back, for one player's bots",
}
KNOWN_EXTRA_METHODS = {
    "ForeverData": {"get": "our extension (E3)", "set": "our extension (E3)",
                    "delete": "our extension (E3)", "keys": "our extension (E3)"},
    "Bot": {name: "our extension (B1)" for name in (
        "GetGUID", "IsValid", "GetName", "GetLevel", "GetClass", "GetRole", "IsRoleForced", "GetSpec", "HasEngine",
        "GetEngineState", "IsAlive", "IsInCombat", "GetMaster", "GetCommander", "GetGroup", "GetPlayer",
        "GetLastOrder", "GetRun")},
}
KNOWN_EXTRA_METHODS["Bot"].update({name: "our extension (B2): the order of this bot" for name in (
    "Follow", "Stay", "Come", "Attack", "Pull", "EndPull", "Hold", "Free", "Assist", "Release", "Stop", "Flee", "Guard",
    "Stance", "Engage", "Rebuff", "SetToggle", "CorpseRun", "CrowdControl", "ClearCrowdControl", "StartRun", "StopRun",
    "PauseRun", "ResumeRun")})

# Facts from the documentation/spec that the report states next to an event (kept short and sourced).
EVENT_NOTES = {
    ("server", 6): "the spec names no driving hook (section 3: none); the engine registers it and logs nothing",
    ("server", 17): "world (continent) maps only; dungeons and raids need C11",
    ("server", 18): "world (continent) maps only; dungeons and raids need C11",
    ("server", 21): "world (continent) maps only; dungeons and raids need C11",
    ("server", 22): "world (continent) maps only; dungeons and raids need C11",
    ("server", 23): "world (continent) maps only; dungeons and raids need C11",
    ("player gossip", 1): "the documentation says HELLO is not applicable to player gossip; the engine registers it",
    ("packet", 5): "read-only copy of the core packet (ServerScript hook): a returned false cannot change the wire packet",
    ("packet", 7): "read-only copy of the core packet (ServerScript hook): a returned false cannot change the wire packet",
    ("item", 3): "fires for items whose ScriptName is forever_lua_item",
    ("item", 4): "fires for items whose ScriptName is forever_lua_item",
    ("item", 5): "fires for items whose ScriptName is forever_lua_item",
}

# Engine caveats of a documented name, from README.md ('Eluna mode') and knowledge/modes/eluna-mode.md.
METHOD_NOTES = {
    ("Guild", "AddMember"): "an explicit rankId the guild has no rank for is refused and nothing is written (the "
                            "core would otherwise store it and leave the member without a rank)",
    ("Object", "GetGUID"): "returns all 128 bits as a 32-digit hex string (the docs type is a 64-bit number); the "
                           "type, entry and map live in the high half",
    ("WorldPacket", "GetOpcode"): "only what CreatePacket made or a packet event passes; the events get a read-only copy",
}
GLOBAL_NOTES = {
    "PerformIngameSpawn": "save writes the spawn to the creature/gameobject table (eluna mode trusts its scripts)",
    "GetCoreName": "answers ForeverCore",
    "GetLuaEngine": "answers ForeverLua",
    "GetCoreExpansion": "answers 0 (Classic 1.60)",
}

STUB_MARKERS = [
    (re.compile(r"not available on Classic 1\.60"), "Classic 1.60"),
    (re.compile(r"needs core hook (C\d+)"), "core hook"),
    (re.compile(r"not available on this server"), "this server"),
]

FUNCTION_KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch", "do", "else"}


class ParseError(SystemExit):
    pass


# ---------------------------------------------------------------------------------------------------------------
# C++ reading

def read(path):
    return path.read_text(encoding="utf-8")


def strip_comments(text):
    """Removes // and /* */ comments, keeping string and character literals intact."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\":
                    i += 2
                    out.append(text[i - 1] if i - 1 < n else "")
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def matching(text, start, open_ch, close_ch):
    """Index of the bracket closing the one at `start` (which holds open_ch)."""
    depth = 0
    i, n = start, len(text)
    while i < n:
        c = text[i]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ParseError(f"unbalanced {open_ch}{close_ch} starting at byte {start}")


def functions(text):
    """name -> body of every function definition in `text` (first definition wins; an overload is merged)."""
    found = {}
    for m in re.finditer(r"(?<![.\w>])([A-Za-z_]\w*)\s*\(", text):
        name = m.group(1)
        if name in FUNCTION_KEYWORDS or name in found:
            continue
        close = matching(text, m.end() - 1, "(", ")")
        k = close + 1
        while k < len(text) and text[k] in " \t\r\n":
            k += 1
        # Skip const / noexcept / override / -> type before the body.
        for _ in range(4):
            word = re.match(r"(const|noexcept|override|final)\b", text[k:])
            if word:
                k += word.end()
                while k < len(text) and text[k] in " \t\r\n":
                    k += 1
                continue
            arrow = re.match(r"->\s*[\w:<>\s,&*]+?(?=\{)", text[k:])
            if arrow:
                k += arrow.end()
                continue
            break
        if k >= len(text) or text[k] != "{":
            continue
        end = matching(text, k, "{", "}")
        found[name] = text[k + 1:end]
    return found


def single_forward(body):
    """The function name when the body is only `return name(...)` (optionally preceded by argument checks)."""
    m = re.search(r"\breturn\s+([A-Za-z_]\w*)\s*\(", body)
    if not m:
        return None
    close = matching(body, m.end() - 1, "(", ")")
    rest = body[close + 1:].strip()
    return m.group(1) if rest == ";" else None


def stub_reason(body, fns, depth=0):
    """The stub note of a function body (or of the function it only forwards to), else None."""
    for pattern, label in STUB_MARKERS:
        m = pattern.search(body)
        if m:
            if label == "core hook":
                return f"stub: needs core hook {m.group(1)}"
            return f"stub: not available ({label})"
    if depth == 0:
        forward = single_forward(body)
        if forward and forward in fns:
            return stub_reason(fns[forward], fns, depth + 1)
    return None


def strip_qualifier(name):
    return name.rsplit("::", 1)[-1]


def token(name):
    """A name reduced to its words: on_login -> ('login',), LUA_STATE_CLOSE -> ('lua','state','close')."""
    n = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    n = re.sub(r"^on_", "", n.lower())
    return tuple(w for w in re.split(r"[_\s]+", n) if w)


# ---------------------------------------------------------------------------------------------------------------
# Engine surface

class Engine:
    def __init__(self):
        self.files = {}
        self.fns = {}
        for path in sorted(ELU_DIR.glob("*.cpp")) + [ENGINE_DIR / "EluAlias.cpp", ENGINE_DIR / "Api.cpp",
                                                     ENGINE_DIR / "Engine.cpp"]:
            text = strip_comments(read(path))
            self.files[path.name] = text
            for name, body in functions(text).items():
                self.fns.setdefault(name, body)

        self.globals = {}      # name -> {"fn": ..., "table": {sub: fn} or None}
        self.methods = {}      # class -> {name: {"stub": bool, "reason": str|None, "declared": class}}
        self.inherits = {}     # class -> parent or None
        self.events = {}       # family -> {id: name}
        self.test_mode = {}    # toolkit table -> methods
        self.test_globals = []  # test-mode global tables
        self._scan_classes()
        self._scan_eluna_globals()
        self._scan_test_mode()

    def _body(self, name):
        if name not in self.fns:
            raise ParseError(f"the engine registration refers to {name}(), which the scanner did not find")
        return self.fns[name]

    def _scan_classes(self):
        alias = self.files["EluAlias.cpp"]
        decl = functions(alias).get("registerElunaClasses", "")
        for m in re.finditer(r"createClass\(L,\s*\"(\w+)\",\s*(nullptr|\"(\w+)\")\)", decl):
            self.inherits[m.group(1)] = m.group(3)
        if not self.inherits:
            raise ParseError("no createClass() calls found in registerElunaClasses")

        for name, body in self.fns.items():
            if not name.startswith("register") and not name.startswith("flu::"):
                continue
            tables = {}
            for tm in re.finditer(r"(?:static\s+)?luaL_Reg\s+const\s+(\w+)\s*\[\]\s*=\s*\{", body):
                end = matching(body, tm.end() - 1, "{", "}")
                tables[tm.group(1)] = re.findall(r'\{\s*"(\w+)"\s*,\s*(?:fl::)?safe<(?:\w+::)?(\w+)>', body[tm.end():end])
            for am in re.finditer(r"addMethods\(L,\s*\"(\w+)\",\s*(\w+)\)", body):
                cls, table = am.group(1), am.group(2)
                if table not in tables:
                    raise ParseError(f"{name}: addMethods on {cls} uses table {table}, which the scanner did not find")
                self.methods.setdefault(cls, {})
                for method, fn in tables[table]:
                    self.methods[cls].setdefault(method, {"stub": False, "reason": None, "declared": cls, "fn": fn})
            # Register*Event registration inside the classes' own functions is handled below.
            for loop in re.finditer(r"for\s*\(\s*char\s+const\s*\*\s*(\w+)\s*:\s*\{([^}]*)\}\s*\)\s*"
                                    r"addStubMethod\(L,\s*\"(\w+)\",\s*\1\);", body):
                for method in re.findall(r'"(\w+)"', loop.group(2)):
                    self._add_stub(loop.group(3), method)
            for call in re.finditer(r"addStubMethod\(L,\s*\"(\w+)\",\s*\"(\w+)\"\);", body):
                self._add_stub(call.group(1), call.group(2))

        if "registerStubMethods" not in self.fns:
            raise ParseError("registerStubMethods() not found: the section 7 stubs would be missed")
        for cls, methods in self.methods.items():
            for method, info in methods.items():
                if not info["stub"]:
                    info["reason"] = stub_reason(self._body(info["fn"]), self.fns)

    def _add_stub(self, cls, method):
        self.methods.setdefault(cls, {})
        self.methods[cls][method] = {"stub": True, "reason": "stub: not available (Classic 1.60)",
                                    "declared": cls, "fn": None}

    def _scan_eluna_globals(self):
        for register in ("registerGlobals", "registerGameGlobals"):
            body = self._body(register)
            for m in re.finditer(r"set\(L,\s*\"(\w+)\",\s*(?:fl::)?safe<(?:\w+::)?(\w+)>\)", body):
                self.globals[m.group(1)] = {"fn": m.group(2), "table": None, "where": register}
            if not any(True for _ in re.finditer(r"set\(L,", body)):
                raise ParseError(f"{register}(): no set(L, ...) globals found")
        # The ForeverData table (E3), its sub-functions read from registerForeverData's own body.
        data = self._body("registerForeverData")
        for m in re.finditer(r"registerForeverData\(L,\s*\"(\w+)\"\)", self.fns["registerGameGlobals"]):
            subs = re.findall(r"lua_setfield\(L,\s*-2,\s*\"(\w+)\"\)", data)
            self.globals[m.group(1)] = {"fn": None, "table": subs, "where": "registerGameGlobals"}
        # print/require: installed by registerCommon in both modes.
        common = self._body("registerCommon")
        for m in re.finditer(r"lua_setglobal\(L,\s*\"(\w+)\"\)", common):
            self.globals[m.group(1)] = {"fn": None, "table": None, "where": "registerCommon"}
        for name, info in self.globals.items():
            info["reason"] = stub_reason(self._body(info["fn"]), self.fns) if info["fn"] else None

    def _scan_test_mode(self):
        body = self._body("registerApi")
        tables = {}
        for tm in re.finditer(r"luaL_Reg\s+const\s+(\w+)\s*\[\]\s*=\s*\{", body):
            end = matching(body, tm.end() - 1, "{", "}")
            tables[tm.group(1)] = re.findall(r'\{\s*"(\w+)"\s*,', body[tm.end():end])
        for m in re.finditer(r"setFuncs\(L,\s*\"(\w+)\",\s*(\w+)\);", body):
            if m.group(2) not in tables:
                raise ParseError(f"registerApi: setFuncs uses table {m.group(2)}, which the scanner did not find")
            self.test_mode[m.group(1)] = tables[m.group(2)]
            self.test_globals.append(m.group(1))
        if not self.test_mode:
            raise ParseError("registerApi(): no setFuncs tables found")
        # Data (the test mode's ForeverData alias).
        data = self._body("registerForeverData")
        for m in re.finditer(r"registerForeverData\(L,\s*\"(\w+)\"\)", self.files["Engine.cpp"]):
            self.test_mode[m.group(1)] = re.findall(r"lua_setfield\(L,\s*-2,\s*\"(\w+)\"\)", data)
            self.test_globals.append(m.group(1))


# ---------------------------------------------------------------------------------------------------------------
# Event tables and their notes (EluAlias.cpp, EluCommon.cpp), read with a small evaluator for the conditions

TOKEN = re.compile(r"""\s+|"[^"]*"|\d+|[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*|&&|\|\||==|!=|!|\?|:|\(|\)|\{|\}|,|;|""", re.S)


def tokens(text):
    out = []
    pos = 0
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ParseError(f"cannot tokenize near {text[pos:pos + 40]!r}")
        pos = m.end()
        tok = m.group(0)
        if not tok.strip():
            continue
        out.append(tok)
    return out


class Eval:
    """Evaluates the C++ conditions of hookGapFor/classicStubFor on a core without the C1-C4 hooks."""

    def __init__(self):
        self.conditional = False

    def reset(self):
        self.conditional = False

    def hook(self, name):
        self.conditional = True
        return False   # the pinned core has none of the C1-C4 hooks; C5-C12 exist nowhere

    def expr(self, toks):
        value, i = self._ternary(toks, 0)
        if i != len(toks):
            raise ParseError(f"trailing tokens in expression: {' '.join(toks[i:])}")
        return value

    def _ternary(self, t, i):
        cond, i = self._or(t, i)
        if i < len(t) and t[i] == "?":
            # Both arms are parsed (to advance the index) but only the taken one counts for `conditional`.
            mark = self.conditional
            a, i = self._ternary(t, i + 1)
            flag_a = self.conditional
            if i >= len(t) or t[i] != ":":
                raise ParseError(f"malformed ?: at {' '.join(t[i:])}")
            self.conditional = mark
            b, i = self._ternary(t, i + 1)
            flag_b = self.conditional
            self.conditional = flag_a if cond else flag_b
            return (a if cond else b), i
        return cond, i

    def _or(self, t, i):
        value, i = self._and(t, i)
        while i < len(t) and t[i] == "||":
            rhs, i = self._and(t, i + 1)
            value = bool(value) or bool(rhs)
        return value, i

    def _and(self, t, i):
        value, i = self._cmp(t, i)
        while i < len(t) and t[i] == "&&":
            rhs, i = self._cmp(t, i + 1)
            value = bool(value) and bool(rhs)
        return value, i

    def _cmp(self, t, i):
        value, i = self._unary(t, i)
        while i < len(t) and t[i] in ("==", "!="):
            op = t[i]
            rhs, i = self._unary(t, i + 1)
            value = (value == rhs) if op == "==" else (value != rhs)
        return value, i

    def _unary(self, t, i):
        if i < len(t) and t[i] == "!":
            value, i = self._unary(t, i + 1)
            return (not bool(value)), i
        return self._primary(t, i)

    def _primary(self, t, i):
        if i >= len(t):
            raise ParseError("unexpected end of expression")
        tok = t[i]
        if tok == "(":
            value, i = self._ternary(t, i + 1)
            if i >= len(t) or t[i] != ")":
                raise ParseError(f"unbalanced ( in {' '.join(t)}")
            return value, i + 1
        if tok.startswith('"'):
            return tok[1:-1], i + 1
        if tok.isdigit():
            return int(tok), i + 1
        if tok in ("true", "false"):
            return tok == "true", i + 1
        if tok == "nullptr":
            return None, i + 1
        if tok.endswith("hasCoreHook"):
            # fl::hasCoreHook(cfg, fl::CoreHook::Name)
            if i + 1 >= len(t) or t[i + 1] != "(":
                raise ParseError("hasCoreHook without arguments")
            close = i + 2
            while close < len(t) and t[close] != ")":
                close += 1
            args = " ".join(t[i + 2:close])
            if "CoreHook::" not in args:
                raise ParseError(f"cannot read hasCoreHook arguments: {args}")
            return self.hook(strip_qualifier(args.split("CoreHook::")[1].strip(" )"))), close + 1
        if "::" in tok:
            return strip_qualifier(tok), i + 1
        if i + 1 < len(t) and t[i + 1] == "(":
            raise ParseError(f"unsupported call {tok}() in a condition")
        return self.env(tok), i + 1

    def env(self, name):
        raise ParseError(f"unknown identifier {name} in an event-note condition")


class NoteEval(Eval):
    def __init__(self, conds, kind=None, event=None):
        super().__init__()
        self.conds = conds
        self.kind = kind
        self.event = event

    def env(self, name):
        if name in self.conds:
            return self.hook(name) if self.conds[name] == "hook" else self.conds[name]
        if name == "event":
            return self.event
        if name == "kind":
            return self.kind
        raise ParseError(f"unknown identifier {name} in an event-note condition")


def parse_statement(t, i):
    """Parses one statement: a label, a return, an if, a switch or a block. Returns (item, next index)."""
    tok = t[i]
    if tok == "return":
        j = i + 1
        expr = []
        while j < len(t) and t[j] != ";":
            expr.append(t[j])
            j += 1
        return ("return", expr), j + 1
    if tok == "if":
        if i + 1 >= len(t) or t[i + 1] != "(":
            raise ParseError("if without (")
        close = i + 2
        depth = 1
        while close < len(t) and depth:
            if t[close] == "(":
                depth += 1
            elif t[close] == ")":
                depth -= 1
            close += 1
        if depth:
            raise ParseError("unbalanced if condition")
        cond = t[i + 2:close - 1]
        body, i = parse_statement(t, close)
        return ("if", cond, [body]), i
    if tok == "switch":
        if i + 1 >= len(t) or t[i + 1] != "(":
            raise ParseError("switch without (")
        close = i + 2
        while close < len(t) and t[close] != ")":
            close += 1
        subject = t[i + 2:close]
        if close + 1 >= len(t) or t[close + 1] != "{":
            raise ParseError("switch without a body")
        body, i = parse_items(t, close + 2)
        return ("switch", subject, body), i
    if tok == "{":
        body, i = parse_items(t, i + 1)
        return ("block", body), i
    if tok == ";":
        return ("empty",), i + 1
    raise ParseError(f"unparsed statement {tok!r} in an event-note function")


def parse_items(t, i):
    """Parses `{ ... }` content into items; the closing `}` is consumed."""
    items = []
    while i < len(t):
        tok = t[i]
        if tok == "}":
            return items, i + 1
        if tok == "case":
            if i + 1 >= len(t):
                raise ParseError("case without a label")
            items.append(("label", t[i + 1]))
            i += 2
            if i >= len(t) or t[i] != ":":
                raise ParseError(f"case label without ':' near {' '.join(t[max(0, i - 6):i + 2])}")
            i += 1
            continue
        if tok == "default" and i + 1 < len(t) and t[i + 1] == ":":
            items.append(("default",))
            i += 2
            continue
        item, i = parse_statement(t, i)
        items.append(item)
    raise ParseError("unterminated block in an event-note function")


NO_RESULT = object()


def eval_stmt(item, ev):
    """One statement: the gap string/None of a return (None = nullptr), or NO_RESULT when nothing returns."""
    if item[0] == "return":
        return ev.expr(item[1])
    if item[0] == "empty":
        return NO_RESULT
    if item[0] == "if":
        if ev.expr(item[1]):
            return eval_segment(item[2], 0, ev)[0] if item[2] else NO_RESULT
        return NO_RESULT
    if item[0] == "block":
        return eval_segment(item[1], 0, ev)[0]
    if item[0] == "switch":
        subject = ev.expr(item[1])
        body = item[2]
        start = None
        default = None
        for k, x in enumerate(body):
            if x[0] == "label":
                lab = x[1]
                lab = int(lab) if lab.isdigit() else strip_qualifier(lab)
                if lab == subject:
                    start = k
            elif x[0] == "default":
                default = k
        if start is None:
            start = default
        if start is None:
            return NO_RESULT
        j = start + 1
        while j < len(body) and body[j][0] in ("label", "default"):
            j += 1   # a cluster of labels shares one statement (C fallthrough)
        return eval_segment(body, j, ev)[0]
    raise ParseError(f"unparsed item {item!r}")


def eval_segment(items, i, ev):
    """Evaluates statements from i up to the next label/default (the end of a switch branch)."""
    while i < len(items):
        if items[i][0] in ("label", "default"):
            return NO_RESULT, i
        value = eval_stmt(items[i], ev)
        if value is not NO_RESULT:
            return value, i + 1
        i += 1
    return NO_RESULT, i


class EventNotes:
    """The per-event notes of the engine: hookGapFor and classicStubFor of src/engine/elu/EluCommon.cpp."""

    def __init__(self, fns):
        body = fns.get("hookGapFor")
        if body is None:
            raise ParseError("hookGapFor() not found in EluCommon.cpp")
        self.conds = {}
        for m in re.finditer(r"bool\s+const\s+(\w+)\s*=\s*fl::hasCoreHook\(cfg,\s*fl::CoreHook::(\w+)\);", body):
            self.conds[m.group(1)] = "hook"
        # A non-hook bool (none today) would be a literal; refuse instead of guessing.
        for m in re.finditer(r"bool\s+const\s+(\w+)\s*=([^;]*);", body):
            if m.group(1) not in self.conds:
                raise ParseError(f"hookGapFor: cannot classify '{m.group(0).strip()}'")
        head = body.index("switch (kind)")
        item, _ = parse_statement(tokens(body[head:]), 0)
        if item[0] != "switch":
            raise ParseError("hookGapFor: the body does not start with its switch (kind)")
        self.hook_items = [item]
        self.classic_body = fns.get("classicStubFor")
        if self.classic_body is None:
            raise ParseError("classicStubFor() not found in EluCommon.cpp")
        self.classic_items, _ = parse_items(tokens(self.classic_body + "}"), 0)
        notes = fns.get("noteEventGaps")
        if notes is None:
            raise ParseError("noteEventGaps() not found in EluCommon.cpp")
        m = re.search(r"bool const hooked = (.*?);\n", notes, re.S)
        if not m:
            raise ParseError("noteEventGaps: the 'hooked' rule was not found")
        self.hooked_expr = tokens(m.group(1))

    class _HookedEval(Eval):
        def __init__(self, creature, event):
            super().__init__()
            self.creature = creature
            self.event = event

        def env(self, name):
            if name == "creature":
                return self.creature
            if name == "event":
                return self.event
            raise ParseError(f"unknown identifier {name} in the 'hooked' rule")

    def hooked(self, kind, event):
        """True when the event has a core hook (or the engine's own path); False = the ScriptName AI path only."""
        if kind not in ("Creature", "GameObject"):
            return True
        return bool(self._HookedEval(kind == "Creature", event).expr(self.hooked_expr))

    def hook(self, kind, event):
        ev = NoteEval(self.conds, kind, event)
        ev.reset()
        value, _ = eval_segment(self.hook_items, 0, ev)
        if value is not NO_RESULT and value is not None and not isinstance(value, str):
            raise ParseError(f"hookGapFor({kind}, {event}) answered {value!r}")
        return value, ev.conditional

    def classic(self, kind, event):
        ev = NoteEval(self.conds, kind, event)
        value, _ = eval_segment(self.classic_items, 0, ev)
        return value if isinstance(value, str) else None


def scan_event_tables(alias_text):
    """SpecRow tables of EluAlias.cpp: block name -> {id: name}, and the family -> block map of eluEventSpec."""
    blocks = {}
    for m in re.finditer(r"constexpr\s+SpecRow\s+(\w+)\s*\[\]\s*=\s*\{", alias_text):
        end = matching(alias_text, m.end() - 1, "{", "}")
        rows = {}
        for row in re.finditer(r"\{\s*(\d+)\s*,\s*\{\s*\"(\w+)\"", alias_text[m.end():end]):
            rows[int(row.group(1))] = row.group(2)
        blocks[m.group(1)] = rows
    if not blocks:
        raise ParseError("no SpecRow tables found in EluAlias.cpp")

    spec_fn = functions(alias_text).get("eluEventSpec")
    if spec_fn is None:
        raise ParseError("eluEventSpec() not found in EluAlias.cpp")
    family_block = {}
    current = []
    for line in spec_fn.splitlines():
        for kind in re.findall(r"case\s+(?:fl::)?EluKind::(\w+)", line):
            current.append(kind)
        m = re.search(r"return\s+findSpec\((\w+),\s*event\)", line)
        if m:
            for kind in current:
                family_block[KIND_FAMILY[kind]] = m.group(1)
            current = []
    missing = [f for f in EVENT_FAMILIES if f != "spell" and f not in family_block]
    if missing:
        raise ParseError(f"eluEventSpec does not map {missing}")
    return blocks, family_block


# ---------------------------------------------------------------------------------------------------------------
# The report

def documented(path):
    if not path.exists():
        raise ParseError(f"{path} is missing; run tools/coverage/fetch_documented_api.py first")
    return json.loads(path.read_text(encoding="utf-8"))


class Coverage:
    def __init__(self, engine, doc):
        self.e = engine
        self.doc = doc
        self.class_order = CLASS_FAMILIES + [c for c in OTHER_CLASS_FAMILIES if c not in CLASS_FAMILIES]
        self.rows = []            # summary rows
        self.blocks = []          # markdown blocks
        self.json = {"format": 1, "mode": "eluna",
                     "documented_api": "docs/coverage/eluna-documented-api.json",
                     "globals": {}, "classes": {}, "events": {}, "missing": {}, "extra": {},
                     "test_mode_only": {}}

    # -- helpers ---------------------------------------------------------------------------------------------
    def chain(self, cls):
        """The class and its engine parents, in method resolution order."""
        out, seen = [], set()
        while cls and cls not in seen:
            seen.add(cls)
            out.append(cls)
            cls = self.e.inherits.get(cls)
        return out

    def resolve(self, cls, method):
        for klass in self.chain(cls):
            if method in self.e.methods.get(klass, {}):
                return klass, self.e.methods[klass][method]
        return None, None

    def add_block(self, text):
        self.blocks.append(text)

    # -- globals ---------------------------------------------------------------------------------------------
    def globals(self):
        documented_names = list(self.doc["globals"].keys())
        rows = []
        for name in documented_names:
            info = self.e.globals.get(name)
            if info is None:
                status = "missing"
                row = {"status": status}
            else:
                status = "stub" if info.get("reason") else "implemented"
                row = {"status": status}
                if info.get("reason"):
                    row["note"] = info["reason"]
                if info.get("table") is not None:
                    row["table"] = info["table"]
            if GLOBAL_NOTES.get(name):
                row = dict(row)
                row["note"] = (row.get("note") + "; " if row.get("note") else "") + GLOBAL_NOTES[name]
            self.json["globals"][name] = row
            rows.append((name, row))
        extra = []
        for name in sorted(self.e.globals):
            if name in self.doc["globals"]:
                continue
            info = self.e.globals[name]
            note = KNOWN_EXTRA_GLOBALS.get(name)
            if note is None and info.get("table") is not None:
                note = None
            extra.append((name, {"status": "extra", "note": note, "table": info.get("table")}))
            self.json["extra"].setdefault("globals", {})[name] = {"note": note, "table": info.get("table")}
        missing = [n for n, r in rows if r["status"] == "missing"]
        stub = [n for n, r in rows if r["status"] == "stub"]
        self.rows.append({"family": "Global", "kind": "globals", "documented": len(rows),
                          "implemented": len(rows) - len(missing) - len(stub), "stub": len(stub),
                          "missing": len(missing)})
        self.json["missing"]["globals"] = sorted(missing)

        lines = ["### Global functions", "",
                 "| Documented global | Status | Note |", "|---|---|---|"]
        for name, row in rows:
            lines.append(f"| `{name}` | {cell_status(row['status'])} | {row.get('note', '')} |")
        self.add_block("\n".join(lines))
        if extra:
            lines = ["### Global extras (in the engine, not in the documented API)", "",
                     "| Name | Note |", "|---|---|"]
            for name, row in extra:
                note = row["note"] or ("table: " + ", ".join(row["table"]) if row.get("table") else "not in the spec's global list")
                lines.append(f"| `{name}` | {note} |")
            self.add_block("\n".join(lines) + "\n")
        return rows

    # -- classes ---------------------------------------------------------------------------------------------
    def classes(self):
        all_documented = set()
        for methods in self.doc["classes"].values():
            all_documented.update(methods)
        for cls in self.class_order:
            if cls not in self.doc["classes"] and cls not in self.e.methods:
                continue
            documented_methods = self.doc["classes"].get(cls, {})
            rows = []
            for name in sorted(documented_methods, key=str.lower):
                declared, info = self.resolve(cls, name)
                if declared is None:
                    row = {"status": "missing"}
                    # Registered on a subclass only: not available on every object of this class.
                    providers = [d for d in self.e.methods
                                 if d != cls and cls in self.chain(d) and name in self.e.methods[d]]
                    if providers:
                        row["note"] = "the engine registers it on " + ", ".join(
                            f"`{d}`" for d in sorted(providers, key=str.lower)) + " only"
                elif info.get("reason"):
                    row = {"status": "stub", "note": info["reason"], "declared": declared}
                else:
                    row = {"status": "implemented", "declared": declared}
                if METHOD_NOTES.get((cls, name)):
                    row = dict(row)
                    row["note"] = (row.get("note") + "; " if row.get("note") else "") + METHOD_NOTES[(cls, name)]
                self.json["classes"].setdefault(cls, {"inherits": self.e.inherits.get(cls), "methods": {}})
                self.json["classes"][cls]["methods"][name] = row
                rows.append((name, row))
            extra = []
            for name in sorted(self.e.methods.get(cls, {}), key=str.lower):
                if name in all_documented and cls in self.doc["classes"]:
                    continue
                note = KNOWN_EXTRA_METHODS.get(cls, {}).get(name)
                extra.append((name, note))
                self.json["extra"].setdefault("classes", {}).setdefault(cls, {})[name] = note
            missing = [n for n, r in rows if r["status"] == "missing"]
            stub = [n for n, r in rows if r["status"] == "stub"]
            if documented_methods:
                self.rows.append({"family": cls, "kind": "class", "documented": len(rows),
                                  "implemented": len(rows) - len(missing) - len(stub), "stub": len(stub),
                                  "missing": len(missing)})
            if missing:
                self.json["missing"].setdefault("classes", {})[cls] = missing

            lines = [f"### Class `{cls}`" +
                     (f" (inherits `{self.e.inherits[cls]}`)" if self.e.inherits.get(cls) else ""), "",
                     "| Documented method | Status | Note |", "|---|---|---|"]
            if not documented_methods:
                lines = [f"### Class `{cls}` (engine only: no documented counterpart)", ""]
            for name, row in rows:
                note = row.get("note", "")
                if row["status"] == "implemented" and row["declared"] != cls:
                    note = note or f"declared on `{row['declared']}`"
                lines.append(f"| `{name}` | {cell_status(row['status'])} | {note} |")
            self.add_block("\n".join(lines))
            if extra:
                lines = [f"`{cls}` extras (in the engine, not in the documented API): " +
                         ", ".join(f"`{n}`" + (f" ({note})" if note else "") for n, note in extra), ""]
                self.add_block("\n".join(lines) + "\n")

    # -- events ----------------------------------------------------------------------------------------------
    def events(self, notes, blocks, family_block):
        for family in EVENT_FAMILIES:
            doc_family = self.doc["events"].get(family)
            documented_ids = {int(i): v["name"] for i, v in (doc_family or {}).get("ids", {}).items()}
            name_only = (doc_family or {}).get("name_only", [])
            engine_block = blocks.get(family_block.get(family, ""), {}) if family in family_block else {}
            engine_ids = dict(engine_block)
            if not documented_ids and not name_only:
                lines = [f"### Events: {family} (engine only: no documented counterpart)", "",
                         "| id | Registered name |", "|---|---|"]
                for event_id, name in sorted(engine_ids.items()):
                    lines.append(f"| {event_id} | {name} |")
                    self.json["extra"].setdefault("events", {}).setdefault(family, {})[str(event_id)] = name
                self.add_block("\n".join(lines) + "\n")
                continue
            rows = []
            for event_id in sorted(documented_ids):
                name = documented_ids[event_id]
                if event_id not in engine_ids:
                    row = {"status": "missing", "name": name}
                else:
                    row = {"status": "implemented", "name": engine_ids[event_id]}
                    kind = None
                    for k, f in KIND_FAMILY.items():
                        if f == family:
                            kind = k
                            break
                    # Instance events are registered through RegisterInstanceEvent (a C11 stub); the ids of the map
                    # table are what the engine knows, so the note is taken from the map events of the same table.
                    if kind is None:
                        kind = "Map"
                    classic = notes.classic(kind, event_id)
                    gap, conditional = notes.hook(kind, event_id)
                    if classic:
                        row = {"status": "stub", "name": engine_ids[event_id],
                               "note": f"stub: not available ({classic})", "requires": None}
                    elif gap and not conditional:
                        row = {"status": "stub", "name": engine_ids[event_id],
                               "note": f"stub: needs core hook {gap}", "requires": gap}
                    elif gap in ("C1", "C2", "C3"):
                        # The engine's own line names the interim ScriptName path for these three hooks.
                        row = {"status": "implemented", "name": engine_ids[event_id],
                               "requires": gap,
                               "note": f"needs core hook {gap}; the engine names its interim ScriptName path"}
                    elif gap:
                        row = {"status": "stub", "name": engine_ids[event_id], "requires": gap,
                               "note": f"stub: needs core hook {gap}"}
                    else:
                        row = {"status": "implemented", "name": engine_ids[event_id], "requires": None}
                if EVENT_NOTES.get((family, event_id)):
                    row = dict(row)
                    row["note"] = (row.get("note") + "; " if row.get("note") else "") + EVENT_NOTES[(family, event_id)]
                if row["status"] == "implemented" and family in ("creature", "gameobject") and not notes.hooked(
                        "Creature" if family == "creature" else "GameObject", event_id):
                    row = dict(row)
                    what = "creatures whose ScriptName is forever_lua" if family == "creature" else \
                        "game objects whose ScriptName is forever_lua_gameobject"
                    row["note"] = (row.get("note") + "; " if row.get("note") else "") + f"fires only for {what}"
                entry = {k: v for k, v in row.items()}
                self.json["events"].setdefault(family, {})[str(event_id)] = entry
                rows.append((event_id, row))
            for name in name_only:
                self.json["events"].setdefault(family, {})[name] = {"status": "missing", "name": name}
                rows.append((name, {"status": "missing", "name": name, "note": "documented, no id in the spec"}))

            # An event the spec's id table does not carry but the documentation names (matched by its words): the
            # engine provides it, so it is documented and implemented.
            doc_tokens = {token(n) for n in (doc_family or {}).get("docs_names", [])}
            for event_id, name in sorted(engine_ids.items()):
                if event_id in documented_ids or token(name) not in doc_tokens:
                    continue
                row = {"status": "implemented", "name": name,
                       "note": "documented by name; the spec's id table has no id for it", "requires": None}
                self.json["events"].setdefault(family, {})[str(event_id)] = dict(row)
                rows.append((event_id, row))

            missing = [r["name"] for _, r in rows if r["status"] == "missing"]
            stub = [r["name"] for _, r in rows if r["status"] == "stub"]
            self.rows.append({"family": f"events: {family}", "kind": "events", "documented": len(rows),
                              "implemented": len(rows) - len(missing) - len(stub), "stub": len(stub),
                              "missing": len(missing)})
            if missing:
                self.json["missing"].setdefault("events", {})[family] = missing

            matched = {r[0] for r in rows if isinstance(r[0], int)}
            extra = [(i, n) for i, n in sorted(engine_ids.items()) if i not in documented_ids and i not in matched]
            for i, n in extra:
                self.json["extra"].setdefault("events", {}).setdefault(family, {})[str(i)] = n

            if not rows and not extra:
                continue
            lines = [f"### Events: {family}", "",
                     "| id | Documented name | Registered name | Status | Requires | Note |",
                     "|---|---|---|---|---|---|"]
            for key, row in rows:
                if isinstance(key, int):
                    lines.append(f"| {key} | {documented_ids.get(key, '—')} | {row['name']} | {cell_status(row['status'])} | "
                                 f"{row.get('requires') or ''} | {row.get('note', '')} |")
                else:
                    lines.append(f"| — | {row['name']} | — | {cell_status(row['status'])} | "
                                 f"| {row.get('note', '')} |")
            self.add_block("\n".join(lines))
            if extra:
                self.add_block(f"`{family}` engine-only event ids (not in the documented tables): " +
                               ", ".join(f"{i} {n}" for i, n in extra) + "\n")

    def test_mode_names(self):
        """The toolkit tables of Mode = test: in the engine, not in the eluna surface."""
        names = {}
        for table, methods in self.e.test_mode.items():
            names[table] = {"status": "test-mode only",
                            "methods": {m: {"status": "test-mode only"} for m in methods}}
        self.json["test_mode_only"] = names
        self.test_mode_count = sum(len(m) for m in self.e.test_mode.values())

    # -- output ----------------------------------------------------------------------------------------------
    def markdown(self):
        e = self.e
        lines = [
            "# TortuLua: Eluna-mode API coverage",
            "",
            "What an Eluna-style script can call in TortuLua, and every documented Eluna name it cannot. The engine "
            "side is read from the registration code itself (`src/engine/elu/*.cpp`, `src/engine/EluAlias.cpp`), the "
            "documented side from `docs/coverage/eluna-documented-api.json` (Eluna's published documentation plus "
            "the maintainers' API specification, sections 2-4 and 7). Regenerate with `tools/coverage/gen_coverage.py --write`; "
            "`tools/coverage/gen_coverage.py --check` fails when the engine registers a name this file does not list.",
            "",
            "Status is computed for the pinned core **without** the C1-C4 core hooks: an event whose notification "
            "needs a hook registers and logs `needs core hook Cn` once. `Requires` names that hook; a conditional hook "
            "(`C1`/`C2`/`C3`) also has the interim ScriptName path the engine names in that line, an unconditional one "
            "(C5-C11) has no path on this core.",
            "",
            "Status legend: **implemented** = registered and works; **stub** = the call or the registration raises the "
            "documented error (`not available on Classic 1.60`, `needs core hook Cn`); **missing** = documented and not "
            "in the engine; **extra** = in the engine and not in the documented API (our extensions, the Lua sandbox).",
            "",
            "## Summary",
            "",
            "| Family | Documented | Implemented | Stub | Missing | Implemented % | Resolvable % |",
            "|---|---|---|---|---|---|---|",
        ]
        for row in self.rows:
            pct = 100.0 * row["implemented"] / row["documented"] if row["documented"] else 0.0
            res = 100.0 * (row["implemented"] + row["stub"]) / row["documented"] if row["documented"] else 0.0
            lines.append(f"| {row['family']} | {row['documented']} | {row['implemented']} | {row['stub']} | "
                         f"{row['missing']} | {pct:.0f}% | {res:.0f}% |")
        glob = next(r for r in self.rows if r["kind"] == "globals")
        cls = [r for r in self.rows if r["kind"] == "class"]
        evt = [r for r in self.rows if r["kind"] == "events"]
        total_doc = glob["documented"] + sum(r["documented"] for r in cls) + sum(r["documented"] for r in evt)
        total_impl = glob["implemented"] + sum(r["implemented"] for r in cls) + sum(r["implemented"] for r in evt)
        total_stub = glob["stub"] + sum(r["stub"] for r in cls) + sum(r["stub"] for r in evt)
        total_missing = glob["missing"] + sum(r["missing"] for r in cls) + sum(r["missing"] for r in evt)
        lines.append(f"| **all** | **{total_doc}** | **{total_impl}** | **{total_stub}** | **{total_missing}** | "
                     f"**{100.0 * total_impl / total_doc:.0f}%** | "
                     f"**{100.0 * (total_impl + total_stub) / total_doc:.0f}%** |")
        lines += ["",
                  "`Implemented %` counts the names a script can call and get a result; `Resolvable %` also counts the "
                  "documented stubs (the call exists and raises the documented error instead of `attempt to call a nil "
                  "value`). A class row counts the class's own documented methods; inherited ones are counted on the "
                  "class the documentation declares them on.",
                  "",
                  f"Test-mode only: {self.test_mode_count} names in {len(self.e.test_mode)} toolkit tables (`Event`, "
                  "`Timer`, `Cmd`, `World`, `Db`, `Json`, `Gm`, the `Data` alias) are installed with `Mode = test` and "
                  "are not part of the eluna surface (appendix A).",
                  "",
                  "## The surface", ""]
        # Blocks are emitted in the order globals(), classes(), events() built them.
        lines += self.blocks
        lines += self.appendix()
        return "\n".join(lines).rstrip() + "\n"

    def appendix(self):
        e = self.e
        out = ["", "## Appendix A: test-mode surface (not available in eluna mode)", "",
               "These are installed only with `Mode = test` (`registerApi`, and the `Data` alias of `ForeverData`); a "
               "script running in eluna mode does not see them.", "",
               "| Table | Members |", "|---|---|"]
        for table, methods in sorted(e.test_mode.items()):
            out.append(f"| `{table}` | " + ", ".join(f"`{m}`" for m in methods) + " |")
        out += ["", "## Appendix B: documented names with no id in the spec's event tables", "",
                "The documentation pages list event names without numeric ids; the maintainers' API specification, section 3 "
                "carries the ids. Where the documentation lists more names than the spec's table (item, group) or the "
                "spec has no table at all (spell), those names cannot be joined by id and are counted as missing.", ""]
        for family, data in sorted(self.doc["events"].items()):
            if data.get("name_only"):
                out.append(f"* `{family}`: " + ", ".join(f"`{n}`" for n in data["name_only"]))
        out += ["", "## Appendix C: how the report is built", "",
                "* Globals: `set(L, \"Name\", fl::safe<fn>)` in `registerGlobals`/`registerGameGlobals`, the "
                "`ForeverData` table of `registerForeverData`, and `print`/`require` of `registerCommon`.",
                "* Class methods: the `luaL_Reg` tables passed to `addMethods` (plus `Corpse`, `BattleGround`, the "
                "`Map` methods of `registerBattleGroundMethods`, `Aura` where the engine has it) and "
                "the section 7 stubs of `addStubMethod`; inheritance from the `createClass` calls.",
                "* Events: the `SpecRow` tables of `EluAlias.cpp`, joined to the families by `eluEventSpec`; the hook "
                "note comes from `hookGapFor` and the Classic-era note from `classicStubFor` (`EluCommon.cpp`), "
                "evaluated for a core without the C1-C4 hooks.",
                "* Stub detection: the registered C++ function (or the single function it only forwards to) raises "
                "`not available on Classic 1.60`, `needs core hook Cn` or `not available on this server`.", ""]
        return out


def cell_status(status):
    return {"implemented": "implemented", "stub": "**stub**", "missing": "**missing**",
            "extra": "extra"}.get(status, status)


def surface(js):
    """The registrations a coverage.json lists, as three comparable sets (for the --check stale-name message)."""
    globals_ = set(js.get("globals", {})) | set(js.get("extra", {}).get("globals", {}))
    methods = set()
    for cls, data in js.get("classes", {}).items():
        for name in data.get("methods", {}):
            methods.add(f"{cls}.{name}")
    for cls, names in js.get("extra", {}).get("classes", {}).items():
        for name in names:
            methods.add(f"{cls}.{name}")
    events = set()
    for family, data in js.get("events", {}).items():
        for key, row in data.items():
            events.add(f"{family}[{key}]{row.get('name', '') if isinstance(row, dict) else row}")
    for family, names in js.get("extra", {}).get("events", {}).items():
        for key, name in names.items():
            events.add(f"{family}[{key}]{name}")
    return globals_, methods, events


def build():
    engine = Engine()
    doc = documented(DOCS_JSON)
    alias = engine.files["EluAlias.cpp"]
    blocks, family_block = scan_event_tables(alias)
    notes = EventNotes(engine.fns)
    cov = Coverage(engine, doc)
    cov.globals()
    cov.classes()
    cov.events(notes, blocks, family_block)
    cov.test_mode_names()
    md = cov.markdown()
    js = json.dumps(cov.json, indent=1, sort_keys=False) + "\n"
    return md, js, cov


def main():
    ap = argparse.ArgumentParser(description="TortuLua Eluna-mode API coverage report")
    ap.add_argument("--write", action="store_true", help="write docs/COVERAGE.md and docs/coverage/coverage.json")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when the checked-in report does not match the engine (CI gate)")
    ap.add_argument("--md", default=str(REPORT_MD))
    ap.add_argument("--json", default=str(REPORT_JSON))
    args = ap.parse_args()
    md, js, cov = build()
    if args.check:
        ok = True
        md_path, json_path = pathlib.Path(args.md), pathlib.Path(args.json)
        for path, want in ((md_path, md), (json_path, js)):
            if not path.exists():
                print(f"STALE: {path} does not exist", file=sys.stderr)
                ok = False
                continue
            have = path.read_text(encoding="utf-8")
            if have != want:
                ok = False
                print(f"STALE: {path} differs from the engine", file=sys.stderr)
                if path == json_path:
                    try:
                        old = json.loads(have)
                    except json.JSONDecodeError:
                        old = {}
                    for label, was, now in zip(("globals", "class methods", "events"), surface(old), surface(cov.json)):
                        added = sorted(now - was)
                        removed = sorted(was - now)
                        if added:
                            print(f"  engine {label} the report does not list: " + ", ".join(added), file=sys.stderr)
                        if removed:
                            print(f"  {label} in the report but no longer in the engine: " + ", ".join(removed),
                                  file=sys.stderr)
                import difflib
                diff = list(difflib.unified_diff(have.splitlines(), want.splitlines(),
                                                 fromfile=str(path), tofile="regenerated", lineterm="", n=1))
                for line in diff[:60]:
                    print("  " + line, file=sys.stderr)
                print("  run tools/coverage/gen_coverage.py --write", file=sys.stderr)
        if not ok:
            return 1
        print(f"coverage report is current ({pathlib.Path(args.md)}, {pathlib.Path(args.json)})")
        return 0
    if args.write:
        pathlib.Path(args.md).write_text(md, encoding="utf-8")
        pathlib.Path(args.json).write_text(js, encoding="utf-8")
        print(f"wrote {args.md} and {args.json}")
    else:
        sys.stdout.write(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
