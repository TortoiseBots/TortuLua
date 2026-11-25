# TortuLua Type Hints

This directory provides type hint definition files for script authors using the [Lua Language Server](https://luals.github.io/) (`LuaLS` / `sumneko.lua`).

## Features

- **Autocomplete & Signatures**: Full autocomplete for all implemented global functions and class methods.
- **Inheritance Hierarchy**: Proper class inheritance (`Player` → `Unit` → `WorldObject` → `Object`, `Creature` → `Unit`, etc.).
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
