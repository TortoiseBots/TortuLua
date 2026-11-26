---
type: API Reference
title: Bot API (B1 Read + Events, B2 Orders)
description: B1 read/event snapshots and B2 queued bot orders via BotProvider.
tags: [api, bots, events, orders]
status: stable
---

# Provider and Snapshot Isolation

A separate bot module that implements the `fl::BotProvider` interface (`src/engine/BotProvider.h`) registers it with TortuLua.

- **Snapshot model:** The world thread captures one immutable snapshot per tick (`flh::pumpBots()` in `OnUpdate`); Lua states read only the snapshot and never call into the provider directly. On transition to unavailable (shutdown or disable), `BotHub::pump` calls `takeRunEvents` once more before clearing the snapshot, and `stopEngine()` pumps once before `server.stop` so queued run events still reach listeners.
- **No provider:** `IsBotApiAvailable()` returns `false` and bot globals raise `"bots not built"`.
- **State scope:** `RegisterBotEvent` is world-state only (calling it from a map state raises an error).

# B1: Read API and Events

- **`Bot` class (Eluna mode):** Exposes name, level, class, role (and forced flag), spec, engine and state, alive/in-combat flags, master, commander, group, underlying `Player`, last order, and active dungeon run.
- **Eluna events (`RegisterBotEvent(event, fn [, shots])`):** IDs 1-10: `BOT_ADDED`, `BOT_REMOVED`, `ORDER_GIVEN`, `ORDER_DONE`, `ORDER_FAILED`, `BOT_DIED`, `WIPE`, `RUN_STEP`, `RUN_STOPPED`, `RUN_LOST`.
- **Test mode events:** `Event.on("bot.added|removed|order|order_done|order_failed|death|wipe|run_step|run_stop|run_lost", fn)`.
- **Run loss event:**

| ID | Event | Test-mode name | Args | Meaning |
|---|---|---|---|---|
| 10 | `RUN_LOST` | `bot.run_lost` | `count` | the provider dropped `count` run events (the engine did not take them in time); the events after it are the newest ones |

# B2: Queued Orders

- **`Bot` order methods:** `Follow`, `Stay`, `Come`, `Attack`, `Pull`, `EndPull`, `Hold`, `Free`, `Assist`, `Release`, `Stop`, `Flee`, `Guard`, `Stance`, `Engage`, `Rebuff`, `SetToggle`, `CorpseRun`, `CrowdControl`, `ClearCrowdControl`, `StartRun`, `StopRun`, `PauseRun`, `ResumeRun`.
- **Order globals:** `BotsInterrupt(bots, t)` and `BotCommanderPlace(player, words)`.
- **Lifecycle:** Each order returns a `ticket` (or `nil, code, msg`), emits `ORDER_GIVEN`, and completes with exactly one `ORDER_DONE` or `ORDER_FAILED` (with code and message). External orders (`.tb order`, run executors) emit the same events with ticket `0`.
- **No order is lost silently:** a reload or a closed state cancels an order still queued before it reaches the provider and ends it `ORDER_FAILED` `cancelled`; an order already handed over keeps its ticket and its terminal event. An order is either committed before the reload or cancelled before the hand-off — never both, never neither. A provider that goes away ends every open ticket of every state `ORDER_FAILED` `unavailable` at the next tick, once each; an order the provider leaves unanswered after it went away inside `submit()` ends `unavailable` too (an available provider that answers nothing ends `failed`).
- **Delivery:** every order event goes to the world state, never to the posting state. A ticket committed before a `.lua reload` reports its `ORDER_GIVEN`/`ORDER_DONE`/`ORDER_FAILED` into the new world scripts (ticket ids are unique, so it cannot collide); there is no separate orphaned event. Map-state scripts never receive order events; they only post.
- **Cross-map safety and rate cap:** Orders queue to the world thread using 32-hex GUIDs (no cross-map `Player` reads). Rate limit: 20 orders in a burst, then 1 per 100 ms per script. Live selftest: `scripts/selftest/bot_spread_on_low_hp.lua`.

Related: [Eluna Mode API](eluna-mode.md), [Test Mode API](test-mode.md).
