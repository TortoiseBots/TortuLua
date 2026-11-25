#!/usr/bin/env python3
"""Unit tests for the editor type hints generator (tools/typehints/gen_typehints.py).

Covers:
  1. An implemented method appears with the right parameter count
  2. A stub is marked deprecated
  3. A missing name is absent
  4. Class inheritance is emitted
  5. --check detects a stale or missing file
  6. Engine extensions are emitted and annotated
  7. Generated Lua files are syntactically valid (luac -p)
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

# Ensure tools/typehints is on sys.path
THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from gen_typehints import (
    check_types,
    generate_all_types,
    generate_class_lua,
    generate_events_lua,
    generate_globals_lua,
    write_types,
)


class TestTypeHintsGenerator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types_dir = REPO_ROOT / "types"
        cls.cov_path = REPO_ROOT / "docs" / "coverage" / "coverage.json"
        cls.doc_path = REPO_ROOT / "docs" / "coverage" / "eluna-documented-api.json"
        cls.src_dir = REPO_ROOT / "src" / "engine"
        cls.files, cls.summary = generate_all_types(cls.cov_path, cls.doc_path, cls.src_dir)

    def test_implemented_method_parameter_count(self):
        """Implemented methods appear with the right parameter count and @param annotations."""
        # 1. Test in the generated Player.lua
        player_lua = self.files.get("Player.lua")
        self.assertIsNotNone(player_lua)

        # Player:AddItem(entry, arg2) has 2 parameters
        m = re.search(r"function\s+Player:AddItem\(([^)]*)\)", player_lua)
        self.assertIsNotNone(m, "Player:AddItem not found in Player.lua")
        params = [p.strip() for p in m.group(1).split(",") if p.strip()]
        self.assertEqual(len(params), 2, f"Player:AddItem expected 2 parameters, got {params}")
        self.assertEqual(params[0], "entry")
        # Ensure 2 @param annotations precede AddItem
        add_item_block = player_lua[max(0, m.start() - 200) : m.start()]
        self.assertEqual(add_item_block.count("---@param"), 2)

        # Player:SendBroadcastMessage(message) has 1 parameter
        m = re.search(r"function\s+Player:SendBroadcastMessage\(([^)]*)\)", player_lua)
        self.assertIsNotNone(m)
        params = [p.strip() for p in m.group(1).split(",") if p.strip()]
        self.assertEqual(len(params), 1, f"Player:SendBroadcastMessage expected 1 parameter, got {params}")
        self.assertEqual(params[0], "message")

        # Player:GetAccountId() has 0 parameters
        m = re.search(r"function\s+Player:GetAccountId\(([^)]*)\)", player_lua)
        self.assertIsNotNone(m)
        params = [p.strip() for p in m.group(1).split(",") if p.strip()]
        self.assertEqual(len(params), 0, f"Player:GetAccountId expected 0 parameters, got {params}")

        # 2. Test programmatic unit: doc_methods parameter overrides
        mock_methods = {
            "CustomMethod": {"status": "implemented", "declared": "TestClass"}
        }
        mock_doc = {
            "CustomMethod": ["target", "amount", "silent"]
        }
        code, _ = generate_class_lua("TestClass", None, mock_methods, mock_doc, scanner=None)
        self.assertIn("function TestClass:CustomMethod(target, amount, silent) end", code)
        self.assertIn("---@param target any", code)
        self.assertIn("---@param amount any", code)
        self.assertIn("---@param silent any", code)

    def test_stub_marked_deprecated(self):
        """Stubs are marked ---@deprecated with their Classic 1.60 reason."""
        # In Player.lua: GetActiveSpec is a Classic 1.60 stub
        player_lua = self.files.get("Player.lua")
        self.assertIsNotNone(player_lua)
        m = re.search(r"---@deprecated[^\n]*\n---@return any\nfunction\s+Player:GetActiveSpec\(", player_lua)
        self.assertIsNotNone(m, "Player:GetActiveSpec should be marked ---@deprecated")

        # In Vehicle.lua: all methods are Classic 1.60 stubs
        vehicle_lua = self.files.get("Vehicle.lua")
        self.assertIsNotNone(vehicle_lua)
        self.assertIn("---@deprecated", vehicle_lua)
        self.assertIn("function Vehicle:GetPassenger(", vehicle_lua)

        # In globals.lua: AddTaxiPath is a stub
        globals_lua = self.files.get("globals.lua")
        self.assertIsNotNone(globals_lua)
        m = re.search(r"---@deprecated[^\n]*\n---@return any\nfunction\s+AddTaxiPath\(", globals_lua)
        self.assertIsNotNone(m, "AddTaxiPath should be marked ---@deprecated")

        # Programmatic test
        code, _ = generate_class_lua(
            "TestStub", None,
            {"OldOp": {"status": "stub", "note": "stub: not available (Classic 1.60)", "declared": "TestStub"}},
            {}, scanner=None
        )
        self.assertIn("---@deprecated not available (Classic 1.60)", code)

    def test_missing_name_absent(self):
        """Missing methods and globals from coverage.json are omitted."""
        player_lua = self.files.get("Player.lua")
        self.assertIsNotNone(player_lua)

        # Verify missing Player methods from coverage.json are NOT in Player.lua
        cov = json.loads(self.cov_path.read_text(encoding="utf-8"))
        player_methods = cov["classes"]["Player"]["methods"]
        missing_player = [m for m, v in player_methods.items() if v.get("status") == "missing"]
        self.assertTrue(len(missing_player) > 0)

        for m in missing_player:
            self.assertNotRegex(
                player_lua,
                rf"\bfunction\s+Player:{m}\b",
                f"Missing method {m} must not be declared in Player.lua"
            )

        # Verify missing global functions are not in globals.lua
        globals_lua = self.files.get("globals.lua")
        cov_globals = cov["globals"]
        missing_globals = [g for g, v in cov_globals.items() if v.get("status") == "missing"]
        for g in missing_globals:
            self.assertNotRegex(
                globals_lua,
                rf"\bfunction\s+{g}\b",
                f"Missing global {g} must not be declared in globals.lua"
            )

    def test_inheritance_emitted(self):
        """Class inheritance is properly emitted as ---@class ClassName : ParentName."""
        cov = json.loads(self.cov_path.read_text(encoding="utf-8"))
        classes = cov["classes"]

        for cls, info in classes.items():
            parent = info.get("inherits")
            lua_file = self.files.get(f"{cls}.lua")
            self.assertIsNotNone(lua_file, f"Expected {cls}.lua to be generated")

            if parent:
                expected_decl = f"---@class {cls} : {parent}"
            else:
                expected_decl = f"---@class {cls}"
            self.assertIn(
                expected_decl, lua_file,
                f"{cls}.lua should have class annotation '{expected_decl}'"
            )

        # Spot checks for key hierarchies
        self.assertIn("---@class Player : Unit", self.files["Player.lua"])
        self.assertIn("---@class Unit : WorldObject", self.files["Unit.lua"])
        self.assertIn("---@class WorldObject : Object", self.files["WorldObject.lua"])
        self.assertIn("---@class Creature : Unit", self.files["Creature.lua"])
        self.assertIn("---@class GameObject : WorldObject", self.files["GameObject.lua"])
        self.assertIn("---@class Item : Object", self.files["Item.lua"])
        self.assertIn("---@class Corpse : WorldObject", self.files["Corpse.lua"])
        self.assertIn("---@class Object\n", self.files["Object.lua"])

    def test_check_detects_stale_file(self):
        """--check mode detects stale, modified, or missing files."""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = pathlib.Path(tmpdir)

            # 1. Fresh write matches
            write_types(tmp_path, self.files)
            ok, errors = check_types(tmp_path, self.files)
            self.assertTrue(ok, f"Expected check_types to pass, but got errors: {errors}")

            # 2. Modify a file -> stale detection
            player_file = tmp_path / "Player.lua"
            orig = player_file.read_text(encoding="utf-8")
            player_file.write_text(orig + "\n-- stale edit\n", encoding="utf-8")
            ok, errors = check_types(tmp_path, self.files)
            self.assertFalse(ok, "Expected check_types to fail on modified file")
            self.assertTrue(any("Player.lua is stale" in e for e in errors))

            # 3. Restore and remove a file -> missing detection
            player_file.write_text(orig, encoding="utf-8")
            unit_file = tmp_path / "Unit.lua"
            unit_file.unlink()
            ok, errors = check_types(tmp_path, self.files)
            self.assertFalse(ok, "Expected check_types to fail on missing file")
            self.assertTrue(any("Unit.lua" in e for e in errors))

            # 4. Unexpected extra file -> detected
            (tmp_path / "Extra.lua").write_text("foo", encoding="utf-8")
            ok, errors = check_types(tmp_path, self.files)
            self.assertFalse(ok, "Expected check_types to fail on unexpected file")
            self.assertTrue(any("Extra.lua" in e for e in errors))

    def test_check_cli(self):
        """The gen_typehints.py --check command-line flag works and exits 0 on valid types/."""
        cmd = [sys.executable, str(THIS_DIR / "gen_typehints.py"), "--check"]
        res = subprocess.run(cmd, cwd=str(REPO_ROOT), capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"--check failed: {res.stderr}\n{res.stdout}")
        self.assertIn("type hints are current", res.stdout)

    def test_extensions_emitted(self):
        """Engine extensions are included and marked with extension notes."""
        # Bot class and methods
        bot_lua = self.files.get("Bot.lua")
        self.assertIsNotNone(bot_lua)
        self.assertIn("---@class Bot", bot_lua)
        self.assertIn("---our extension (B1)", bot_lua)
        self.assertIn("function Bot:GetGUID()", bot_lua)
        self.assertIn("function Bot:GetCommander()", bot_lua)

        # Globals extensions
        globals_lua = self.files.get("globals.lua")
        self.assertIsNotNone(globals_lua)
        self.assertIn("RegisterCommand", globals_lua)
        self.assertIn("ForeverData", globals_lua)
        self.assertIn("IsBotApiAvailable", globals_lua)

        # BotEvents in events.lua
        events_lua = self.files.get("events.lua")
        self.assertIsNotNone(events_lua)
        self.assertIn("---@enum BotEvents", events_lua)
        self.assertIn("BOT_ADDED = 1", events_lua)

    def test_syntax_validity_with_luac(self):
        """Every generated Lua file compiles without syntax errors if luac is installed."""
        luac = shutil.which("luac")
        if not luac:
            self.skipTest("luac binary not installed")

        for fname, content in self.files.items():
            if not fname.endswith(".lua"):
                continue
            proc = subprocess.run(
                [luac, "-p", "-"],
                input=content.encode("utf-8"),
                capture_output=True,
            )
            self.assertEqual(
                proc.returncode, 0,
                f"{fname} has syntax errors according to luac:\n{proc.stderr.decode('utf-8')}"
            )


if __name__ == "__main__":
    unittest.main()
