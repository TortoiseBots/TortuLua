#!/usr/bin/env python3
"""Builds docs/coverage/eluna-documented-api.json: the Eluna API surface server owners may rely on.

The file is checked in; gen_coverage.py reads only it and never the network. This script is the manual step that
rebuilds it, and it is the record of where every name came from:

  * the published Eluna documentation (https://elunaluaengine.github.io/): one page per class, the Global page and one
    page per Register*Event function. Only names are taken from it; no page code is read (docs/ELUNA-CLEANROOM.md).
  * the maintainers' API specification (a working document that is not part of this repository), sections 2 (globals), 3 (event ids and their documented names),
    4 (the method list per class and stage) and 7 (the named Classic-era stubs). The spec is the only source of the
    numeric event ids: the documentation pages list event names without ids.

Where the documentation and the spec name the same thing differently (the spec's `AUCTION add/remove/...` versus the
page's `on_auction_add`, `AIUPDATE` versus `on_ai_update`), both spellings are kept and the count of documented event
names per family must agree with the count of documented ids, except for the families where the documentation lists
more events than the spec's id table covers (item, group) or no id table exists at all (spell). Those residuals are
listed as `name_only`; the script fails when a later documentation change makes the numbers disagree in any other way.

Usage:
    tools/coverage/fetch_documented_api.py [--out docs/coverage/eluna-documented-api.json] [--cache DIR]

`--cache DIR` keeps (or reuses) the fetched pages in DIR, so a rerun does not download again.
"""
import argparse
import datetime
import json
import pathlib
import re
import sys
import urllib.request

BASE = "https://elunaluaengine.github.io/"

# The class pages of the documentation index (its own list, read 2026-11-24).
DOC_CLASSES = [
    "Aura", "AuraEffect", "BattleGround", "Corpse", "Creature", "ElunaProcInfo", "ElunaQuery", "GameObject",
    "Group", "Guild", "Item", "Map", "Object", "Player", "Quest", "Spell", "SpellInfo", "Unit", "Vehicle",
    "WorldObject", "WorldPacket",
]

# The pages that carry an event table: (documentation page, family key used by the report).
DOC_EVENT_PAGES = {
    "RegisterPlayerEvent": "player",
    "RegisterServerEvent": "server",
    "RegisterCreatureEvent": "creature",
    "RegisterGameObjectEvent": "gameobject",
    "RegisterItemEvent": "item",
    "RegisterPlayerGossipEvent": "player gossip",
    "RegisterCreatureGossipEvent": "creature gossip",
    "RegisterGameObjectGossipEvent": "gameobject gossip",
    "RegisterItemGossipEvent": "item gossip",
    "RegisterGroupEvent": "group",
    "RegisterGuildEvent": "guild",
    "RegisterMapEvent": "map",
    "RegisterInstanceEvent": "instance",
    "RegisterPacketEvent": "packet",
    "RegisterBGEvent": "battleground",
    "RegisterSpellEvent": "spell",
}

# The API specification, section 2, by stage. Names only, exactly as the spec writes them.
SPEC_GLOBALS = [
    # E1
    "RegisterPlayerEvent", "RegisterCreatureEvent", "RegisterGameObjectEvent", "RegisterItemEvent",
    "RegisterServerEvent", "RegisterPlayerGossipEvent", "RegisterCreatureGossipEvent",
    "RegisterGameObjectGossipEvent", "RegisterItemGossipEvent",
    "CreateLuaEvent", "RemoveEventById", "RemoveEvents",
    "GetPlayerByName", "GetPlayerByGUID", "GetPlayersInWorld", "GetPlayerCount", "SendWorldMessage",
    "PerformIngameSpawn", "GetGameTime", "GetCurrTime", "GetTimeDiff", "GetCoreName", "GetCoreVersion",
    "GetCoreExpansion", "GetLuaEngine", "GetStateMapId", "GetStateInstanceId", "GetGUIDLow", "GetGUIDType",
    "GetGUIDEntry", "GetItemLink", "GetAreaName", "GetMapById", "GetQuest", "RunCommand", "PrintInfo",
    "PrintError", "PrintDebug", "WorldDBQuery", "WorldDBExecute", "CharDBQuery", "CharDBExecute", "AuthDBQuery",
    "AuthDBExecute", "ReloadEluna",
    "bit_and", "bit_or", "bit_xor", "bit_not", "bit_lshift", "bit_rshift",
    # E2
    "RegisterGroupEvent", "RegisterGuildEvent", "RegisterMapEvent", "RegisterInstanceEvent", "RegisterPacketEvent",
    "GetGuildByName", "GetGuildByLeaderGUID", "SaveAllPlayers", "Kick", "Ban", "SendMail", "AddVendorItem",
    "VendorRemoveItem", "VendorRemoveAllItems", "GetActiveGameEvents", "IsGameEventActive", "StartGameEvent",
    "StopGameEvent",
    # E3 (the three asynchronous queries; RegisterCommand and ForeverData are our extensions, not Eluna API)
    "WorldDBQueryAsync", "CharDBQueryAsync", "AuthDBQueryAsync",
    # E5
    "RegisterBGEvent", "CreatePacket", "AddTaxiPath",
    # section 7 (named stubs of Classic-era systems)
    "RegisterVehicleEvent", "GetOwnerHalaa",
]

# Section 4: the method list of each stage, per class, plus the methods section 7 names.
SPEC_METHODS = {
    "Object": [
        "GetEntry", "GetGUID", "GetGUIDLow", "GetTypeId", "IsPlayer", "ToPlayer", "ToCreature", "ToGameObject",
        "ToUnit", "GetScale", "SetScale", "HasFlag", "GetUInt32Value",
    ],
    "WorldObject": [
        "GetName", "GetMap", "GetMapId", "GetInstanceId", "GetZoneId", "GetAreaId", "GetX", "GetY", "GetZ", "GetO",
        "GetLocation", "GetDistance", "IsWithinDistInMap", "GetNearestPlayer", "GetNearestCreature",
        "GetNearestGameObject", "GetPlayersInRange", "GetCreaturesInRange", "SpawnCreature", "SummonGameObject",
        "SendUnitSay", "SendUnitYell", "SendUnitWhisper", "RegisterEvent", "RemoveEventById", "RemoveEvents",
    ],
    "Unit": [
        "GetLevel", "SetLevel", "GetHealth", "SetHealth", "GetMaxHealth", "GetHealthPct", "GetPower", "SetPower",
        "IsAlive", "IsDead", "IsInCombat", "GetVictim", "Attack", "AttackStop", "CastSpell", "CastCustomSpell",
        "AddAura", "RemoveAura", "HasAura", "Kill", "DealDamage", "MoveTo", "MoveFollow", "MoveStop", "MoveHome",
        "SetFaction", "GetFaction", "IsHostileTo", "IsFriendlyTo", "SetDisplayId", "Emote", "GetOwner",
        "GetVehicle",  # section 7 stub
    ],
    "Player": [
        "SendBroadcastMessage", "SendNotification", "GetAccountId", "GetAccountName", "GetGMRank", "IsGM", "GetTeam",
        "GetClass", "GetRace", "GetGender", "GetCoinage", "ModifyMoney", "AddItem", "RemoveItem", "HasItem",
        "GetItemCount", "Teleport", "GiveXP", "SetLevel", "LearnSpell", "RemoveSpell", "HasSpell", "GetGroup",
        "GetGuild", "GetGuildId", "GetQuestStatus", "AddQuest", "CompleteQuest", "HasQuest", "SaveToDB",
        "KickPlayer", "ResurrectPlayer", "GetSelection", "IsAlliance", "IsHorde",
        "GossipMenuAddItem", "GossipSendMenu", "GossipComplete", "GossipClearMenu", "GossipAddQuests",
        "HasAchieved", "SetAchievement", "GetArenaPoints", "GetLFGRole",  # section 7 stubs
    ],
    "Creature": [
        "GetScriptName", "IsElite", "IsBoss", "IsWorldBoss", "GetHomePosition", "Respawn", "DespawnOrUnsummon",
        "SetReactState", "GetAITarget", "GetSpawnId", "SetRespawnDelay",
    ],
    "GameObject": [
        "GetGoState", "SetGoState", "GetLootState", "SetLootState", "Despawn", "Respawn", "IsSpawned",
        "UseDoorOrButton", "GetDBTableGUIDLow",
    ],
    "ElunaQuery": [
        "IsNull", "GetColumnCount", "GetRowCount", "NextRow", "GetRow", "GetBool", "GetUInt8", "GetUInt16",
        "GetUInt32", "GetUInt64", "GetInt8", "GetInt16", "GetInt32", "GetInt64", "GetFloat", "GetDouble",
        "GetString",
    ],
    "Item": [
        "GetItemLink", "GetCount", "SetCount", "GetOwner", "GetEntry", "IsEquipped", "GetBagSlot", "GetSlot",
    ],
    "Group": ["GetMembers", "GetLeaderGUID", "IsRaidGroup", "AddMember", "RemoveMember", "SendPacket"],
    "Guild": ["GetName", "GetMembers", "GetLeaderGUID", "SendPacket", "SetBankTabText"],  # SetBankTabText: sec 7
    "Quest": ["GetId", "GetLevel", "GetTitle", "IsDaily"],
    "Spell": ["GetEntry", "GetCaster", "Cancel", "GetDuration"],
    "Aura": ["GetEntry", "GetCaster", "GetDuration", "SetDuration"],
}

# Section 3: the documented event ids per family with the spec's name.
SPEC_EVENTS = {
    "server": {
        1: "NETWORK_START", 2: "NETWORK_STOP", 3: "SOCKET_OPEN", 4: "SOCKET_CLOSE", 5: "PACKET_RECEIVE",
        7: "PACKET_SEND", 6: "PACKET_RECEIVE_UNKNOWN", 8: "OPEN_STATE_CHANGE", 9: "CONFIG_LOAD",
        11: "SHUTDOWN_INIT", 12: "SHUTDOWN_CANCEL", 13: "UPDATE", 14: "STARTUP", 15: "SHUTDOWN",
        16: "LUA_STATE_CLOSE", 33: "LUA_STATE_OPEN", 17: "MAP_CREATE", 18: "MAP_DESTROY",
        21: "MAP_PLAYER_ENTER", 22: "MAP_PLAYER_LEAVE", 23: "MAP_UPDATE", 19: "GRID_LOAD", 20: "GRID_UNLOAD",
        24: "TRIGGER_ON_TRIGGER", 25: "WEATHER_CHANGE", 26: "AUCTION_ADD", 27: "AUCTION_REMOVE",
        28: "AUCTION_SUCCESSFUL", 29: "AUCTION_EXPIRE", 30: "ADDON_ON_MESSAGE", 31: "DELETE_CREATURE",
        32: "DELETE_GAMEOBJECT", 34: "GAME_EVENT_START", 35: "GAME_EVENT_STOP",
    },
    "player": {
        1: "CHARACTER_CREATE", 2: "CHARACTER_DELETE", 3: "LOGIN", 4: "LOGOUT", 5: "SPELL_CAST",
        6: "KILL_PLAYER", 7: "KILL_CREATURE", 8: "KILLED_BY_CREATURE", 9: "DUEL_REQUEST", 10: "DUEL_START",
        11: "DUEL_END", 12: "GIVE_XP", 13: "LEVEL_CHANGE", 14: "MONEY_CHANGE", 15: "REPUTATION_CHANGE",
        16: "TALENTS_CHANGE", 17: "TALENTS_RESET", 18: "CHAT", 19: "WHISPER", 20: "GROUP_CHAT", 21: "GUILD_CHAT",
        22: "CHANNEL_CHAT", 23: "EMOTE", 24: "TEXT_EMOTE", 25: "SAVE", 26: "BIND_TO_INSTANCE",
        27: "UPDATE_ZONE", 28: "MAP_CHANGE", 29: "EQUIP", 30: "FIRST_LOGIN", 31: "CAN_USE_ITEM",
        32: "LOOT_ITEM", 33: "ENTER_COMBAT", 34: "LEAVE_COMBAT", 35: "REPOP", 36: "RESURRECT",
        37: "LOOT_MONEY", 38: "QUEST_ABANDON", 39: "LEARN_TALENTS", 40: "ENVIRONMENTAL_DEATH",
        41: "TRADE_ACCEPT", 42: "COMMAND", 43: "SKILL_CHANGE", 44: "LEARN_SPELL", 45: "ACHIEVEMENT_COMPLETE",
        46: "DISCOVER_TAXI", 47: "UPDATE_AREA", 48: "CAN_INIT_TRADE", 49: "CAN_SEND_MAIL", 50: "CAN_JOIN_LFG",
    },
    "creature": {
        1: "ENTER_COMBAT", 2: "LEAVE_COMBAT", 3: "TARGET_DIED", 4: "DIED", 5: "SPAWN", 6: "REACH_WP",
        7: "AIUPDATE", 8: "RECEIVE_EMOTE", 9: "DAMAGE_TAKEN", 10: "PRE_COMBAT", 12: "OWNER_ATTACKED",
        13: "OWNER_ATTACKED_AT", 14: "HIT_BY_SPELL", 15: "SPELL_HIT_TARGET", 19: "JUST_SUMMONED_CREATURE",
        20: "SUMMONED_CREATURE_DESPAWN", 21: "SUMMONED_CREATURE_DIED", 22: "SUMMONED", 23: "RESET",
        24: "REACH_HOME", 26: "CORPSE_REMOVED", 27: "MOVE_IN_LOS", 30: "DUMMY_EFFECT", 31: "QUEST_ACCEPT",
        34: "QUEST_REWARD", 35: "DIALOG_STATUS", 36: "ADD", 37: "REMOVE",
    },
    "gameobject": {
        1: "AIUPDATE", 2: "SPAWN", 3: "DUMMY_EFFECT", 4: "QUEST_ACCEPT", 5: "QUEST_REWARD", 6: "DIALOG_STATUS",
        7: "DESTROYED", 8: "DAMAGED", 9: "LOOT_STATE_CHANGE", 10: "GO_STATE_CHANGED", 12: "ADD", 13: "REMOVE",
        14: "USE",
    },
    "item": {1: "DUMMY_EFFECT", 2: "USE", 3: "QUEST_ACCEPT", 4: "EXPIRE", 5: "REMOVE"},
    "player gossip": {1: "HELLO", 2: "SELECT"},
    "creature gossip": {1: "HELLO", 2: "SELECT"},
    "gameobject gossip": {1: "HELLO", 2: "SELECT"},
    "item gossip": {1: "HELLO", 2: "SELECT"},
    "group": {1: "MEMBER_ADD", 2: "MEMBER_INVITE", 3: "MEMBER_REMOVE", 4: "LEADER_CHANGE", 5: "DISBAND",
              6: "CREATE"},
    "guild": {
        1: "ADD_MEMBER", 2: "REMOVE_MEMBER", 3: "MOTD_CHANGE", 4: "INFO_CHANGE", 5: "CREATE", 6: "DISBAND",
        7: "MONEY_WITHDRAW", 8: "MONEY_DEPOSIT", 9: "ITEM_MOVE", 10: "EVENT", 11: "BANK_EVENT",
    },
    "map": {1: "INITIALIZE", 2: "LOAD", 3: "UPDATE", 4: "PLAYER_ENTER", 5: "CREATURE_CREATE",
            6: "GAMEOBJECT_CREATE", 7: "CHECK_ENCOUNTER"},
    "instance": {1: "INITIALIZE", 2: "LOAD", 3: "UPDATE", 4: "PLAYER_ENTER", 5: "CREATURE_CREATE",
                 6: "GAMEOBJECT_CREATE", 7: "CHECK_ENCOUNTER"},
    "packet": {5: "PACKET_RECEIVE", 7: "PACKET_SEND"},
    "battleground": {1: "START", 2: "END", 3: "CREATE", 4: "PRE_DESTROY"},
}

# Families where the documentation lists event names the spec's id table does not cover. Each such name is documented
# and has no id in the spec; the report counts it as a documented event the engine cannot register. The list is
# checked against the page (the count must match) so a documentation change fails this script instead of passing.
NAME_ONLY = {
    "item": ["on_add", "on_equip", "on_unequip"],
    "group": ["on_member_accept"],
    "spell": None,  # no id table in the spec at all: every name of the page
}


def fetch(url, cache):
    """Returns the page text, from the cache dir when present."""
    if cache is not None:
        cached = cache / re.sub(r"[^A-Za-z0-9_.-]", "_", url[len(BASE):])
        if cached.exists():
            return cached.read_text(encoding="utf-8")
    with urllib.request.urlopen(url, timeout=60) as r:
        text = r.read().decode("utf-8")
    if cache is not None:
        cache.mkdir(parents=True, exist_ok=True)
        cached = cache / re.sub(r"[^A-Za-z0-9_.-]", "_", url[len(BASE):])
        cached.write_text(text, encoding="utf-8")
    return text


def class_methods(page):
    """The names in the page's own function list (`<a class='fn' ...>Name</a>`), in page order, deduplicated."""
    out = []
    for m in re.finditer(r"class='fn'[^>]*>([^<]+)</a>", page):
        name = m.group(1).strip()
        if name and name not in out:
            out.append(name)
    return out


def class_inherits(page):
    """The page's "Inherits all methods from: A, B" line as a list ([] for "none" or no line)."""
    m = re.search(r"Inherits all methods from:(.*?)</p>", page, re.S)
    if not m:
        return []
    text = re.sub(r"<[^>]+>", " ", m.group(1))
    text = re.sub(r"\s+", " ", text).strip()
    if not text or text.lower() == "none":
        return []
    return [p.strip() for p in text.split(",") if p.strip()]


def event_names(page):
    """The documented event names of a Register*Event page (`events.player.on_login` -> `on_login`), page order.

    A page may write the id constant instead (`GOSSIP_EVENT_ON_HELLO`); it is read as the same name."""
    out = []
    for _family, name in re.findall(r"events\.([a-z_]+)\.([a-z0-9_]+)", page):
        if name not in out:
            out.append(name)
    for name in re.findall(r"\b[A-Z][A-Z0-9_]*?_EVENT_ON_([A-Z0-9_]+)\b", page):
        name = "on_" + name.lower()
        if name not in out:
            out.append(name)
    return out


def token(name):
    """A name reduced to its words: on_login -> ('login',), LUA_STATE_CLOSE -> ('lua','state','close')."""
    n = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)  # AIUPDATE stays, AddTaxiPath -> Add_Taxi_Path
    n = n.lower()
    n = re.sub(r"^on_", "", n)
    return tuple(w for w in re.split(r"[_\s]+", n) if w)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="docs/coverage/eluna-documented-api.json")
    ap.add_argument("--cache", default=None, help="directory to keep/reuse the fetched pages in")
    args = ap.parse_args()
    cache = pathlib.Path(args.cache) if args.cache else None

    sources = [
        {
            "name": "Eluna API documentation (published)",
            "url": BASE,
            "read": datetime.date.today().isoformat(),
            "what": "each class page's own method list, the Global page's function list, the Register*Event pages' "
                    "event names; names only, no page code",
        },
        {
            "name": "Maintainers' API specification, sections 2, 3, 4, 7",
            "url": "not distributed",
            "read": datetime.date.today().isoformat(),
            "what": "the numeric event ids (the documentation pages carry names only), the per-stage global and method "
                    "lists, the named Classic-era stubs of section 7",
        },
    ]

    # Globals: the Global page plus the spec's stage lists.
    global_page = fetch(BASE + "Global/index.html", cache)
    globals_doc = class_methods(global_page)
    globals_out = {}
    for name in globals_doc:
        globals_out.setdefault(name, []).append("docs")
    for name in SPEC_GLOBALS:
        globals_out.setdefault(name, [])
        if "spec" not in globals_out[name]:
            globals_out[name].append("spec")

    # Classes: own methods from each class page, inherits from the same page, spec additions.
    classes = {}
    inherits = {}
    for cls in DOC_CLASSES:
        page = fetch(f"{BASE}{cls}/index.html", cache)
        own = class_methods(page)
        classes[cls] = {name: ["docs"] for name in own}
        inherits[cls] = class_inherits(page)
    for cls, names in SPEC_METHODS.items():
        classes.setdefault(cls, {})
        for name in names:
            classes[cls].setdefault(name, [])
            if "spec" not in classes[cls][name]:
                classes[cls][name].append("spec")

    # Events: documented ids from the spec, documented names from the pages, the residual name-only lists.
    events = {}
    for page_name, family in DOC_EVENT_PAGES.items():
        page = fetch(f"{BASE}Global/{page_name}.html", cache)
        names = event_names(page)
        ids = {str(i): n for i, n in sorted(SPEC_EVENTS.get(family, {}).items())}
        name_only = []
        if family in NAME_ONLY:
            wanted = NAME_ONLY[family]
            name_only = list(names) if wanted is None else list(wanted)
            for n in name_only:
                if n not in names:
                    raise SystemExit(f"{page_name}: the documented list no longer has {n!r}")
        # The pages and the spec must agree except for the known residuals: every documented name either matches a
        # spec name by its words or is in the residual list.
        declared = len(ids) + len(name_only)
        if len(names) != declared:
            spec_words = {token(n) for n in ids.values()}
            unmatched = [n for n in names if token(n) not in spec_words and n not in name_only]
            raise SystemExit(f"{page_name}: {len(names)} documented names, {declared} expected "
                             f"({len(ids)} ids, {len(name_only)} name-only); unmatched names: {unmatched}")
        events[family] = {
            "ids": {i: {"name": n, "source": ["spec"]} for i, n in ids.items()},
            "docs_names": names,
            "name_only": name_only,
            "docs_page": page_name,
        }

    # "player gossip" has no table on its page (the Player gossip methods are on Player); keep the family with its spec
    # ids so the report can compare it.
    if "player gossip" not in events:
        events["player gossip"] = {
            "ids": {str(i): {"name": n, "source": ["spec"]} for i, n in sorted(SPEC_EVENTS["player gossip"].items())},
            "docs_names": [],
            "name_only": [],
            "docs_page": "RegisterPlayerGossipEvent",
        }

    out = {
        "format": 1,
        "generated": datetime.date.today().isoformat(),
        "sources": sources,
        "inherits": inherits,
        "globals": dict(sorted(globals_out.items())),
        "classes": {c: dict(sorted(m.items())) for c, m in sorted(classes.items())},
        "events": {f: events[f] for f in sorted(events)},
    }
    path = pathlib.Path(args.out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(out, indent=1, sort_keys=False) + "\n", encoding="utf-8")
    print(f"wrote {path}: {len(out['globals'])} globals, "
          f"{sum(len(m) for m in out['classes'].values())} class methods in {len(out['classes'])} classes, "
          f"{sum(len(e['ids']) for e in out['events'].values())} event ids in {len(out['events'])} families, "
          f"{sum(len(e['name_only']) for e in out['events'].values())} documented event names with no id")


if __name__ == "__main__":
    sys.exit(main())
