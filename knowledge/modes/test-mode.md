---
type: API Reference
title: Test Mode API
description: Sandboxed toolkit tables, script loading, handles, events, and Db.select example.
tags: [api, test-mode, sandbox, lua]
status: stable
---

# Script Loading and Handles

- **Load order:** `init.lua` loads first, followed by remaining `*.lua` files in sorted order; errors in one file are logged while subsequent files continue loading.
- **Transactional reload:** `.lua reload` builds a fresh state and swaps it in only if every file loads cleanly; on failure, existing handlers, timers, commands, and globals remain intact. See [GM Commands](../operations/commands.md).
- **Handles:** `player`, `creature`, `gameobject`, and `map` handles store a GUID and map IDs (never raw pointers) and re-query the world on each call. For gone objects, reads return `nil`, actions return `false, "gone"`, and `h:valid()` returns `false`. `==` compares identity (kind, GUID, map, instance); `tostring(h)` returns the GUID string.

# Schema

| Table | Functions |
|---|---|
| `Event` | `on(name, fn [, {entry = n, bots = true}]) -> id`, `off(id) -> bool` |
| `Timer` | `after(ms, fn) -> id`, `every(ms, fn [, count]) -> id`, `cancel(id) -> bool`; 50 ms minimum, one-tick resolution |
| `Cmd` | `register(root, fn, minSecurity) -> true` or `false, "root not configured"`; `fn(ctx)` with `ctx.player`, `ctx.security`, `ctx.tail`, `ctx.args`, `ctx.id` (`#<id>` token), `ctx:reply(text)` |
| `World` | `now()`, `clock()`, `gameTime()`, `log(level, text)`, `player(nameOrGuid)`, `players([{bots = true}])`, `map(mapId [, instanceId])` |
| `Db` | `select(db, table, columns [, where [, {limit, order, desc}]], fn) -> true` or `nil, reason`; `fn(rows)` or `fn(nil, reason)` on a later tick; `has(db, table [, column])` |
| `Json` | `encode(v)`, `decode(s)`, `Json.null` |
| `Gm` | `run(line) -> ok, lines` through the guarded bridge |
| handles | `valid()`, `guid()`, `kind()`, `mapId()`, `instanceId()`, `entry()`, `name()`, `alive()`, `pos()`, `despawn([ms])` |
| `Map` | `id()`, `instance()`, `name()`, `isDungeon()`, `height(x,y,z [, unit])`, `los(x1,y1,z1,x2,y2,z2 [, unit])`, `players()`, `creatures(x,y,z,r [, {entry, alive, dead}])`, `gameObjects(x,y,z,r [, {entry}])`, `spawnCreature(entry,x,y,z,o [, {despawnMs}])`, `find(guid)` |
| `Unit` | `map()`, `level()`, `health()`, `maxHealth()`, `healthPct()`, `inCombat()`, `victim()`, `target()`, `faction()`, `isHostileTo(u)`, `distanceTo(u \| x,y,z)`, `hasAura(id)`, `aura(id)`, `casting()`, `channel()` |
| `Player` | `isBot()`, `security()`, `group()`, `message(text)`, `teleport(map,x,y,z,o)`, `run(line)` |
| `Creature` | `spawnId()`, `home()`, `rank()`, `isBoss()`, `isPet()`, `owner()`, `kill()` |
| `GameObject` | `spawnId()`, `state()`, `lootState()`, `spawned()` |

- **Events:** `server.start` (boolean: `true` after reload), `server.stop`, `player.login`, `player.logout`, `player.chat`, `creature.death`, `creature.spawn`. Map-thread events arrive up to one tick late.
- **`Db` error reasons:** `no_table`, `no_column`, `bad_name`, `bad_value`, `bad_db`, `no_schema`, `busy`, `timeout`, `query_failed`, `too_large`.

# Examples

Asynchronous `Db.select` query (callback runs with the event budget on a later tick):

```lua
Db.select("world", "creature_template", {"entry", "name"}, {minlevel = 1}, {limit = 5, order = "entry"},
    function(rows, why)
        if not rows then World.log("warn", "select failed: " .. tostring(why)) return end
        for _, r in ipairs(rows) do World.log("info", r.entry .. " " .. r.name) end
    end)
```

Related: [Security Model](../architecture/security.md), [Eluna Mode API](eluna-mode.md), [Bot API](bot-api.md).
