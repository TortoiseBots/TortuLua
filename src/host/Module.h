// ForeverLua host side: the one StateSet of the process (the world state and, with MapStates = 1, one state per map
// instance), its config, and the thread-safe pointer the hooks check.
#pragma once

#include "engine/Elu.h"
#include "engine/Engine.h"
#include "engine/StateSet.h"
#include "AccessRules.h"
#include "ForeverHost.h"
#include "Settings.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flh
{
// The settings from worldserver.conf (read at command-table build time and again before the state is created).
ModuleSettings readSettings();
ModuleSettings const& settings();

// The world state's engine, or null (module off, not started yet, or after shutdown). Safe from any thread. Every
// player, server, group and guild event belongs to it; `MapStates = 0` puts the map-bound families there too.
fl::Engine* engine();
// E4: the state that owns one event (the open map state of `map` for the map-bound families, the world state otherwise;
// null = drop it). Pinned while the caller holds it.
std::shared_ptr<fl::Engine> eventEngine(fl::EluKind kind, std::optional<MapKey> map);

// World thread only.
void startEngine();      // creates the state, loads the scripts, fires server.start (first OnBeforeMapsUpdate)
void stopEngine();       // server.stop with the long budget, then shutdown() (OnShutdown)
fl::LoadReport reloadEngine();  // .lua reload: rebuilds the world state and every map state, fires server.start again
// Eluna's ReloadEluna: the host records the request, the world thread performs the reload outside the Lua call.
bool takeReloadRequest();
// World thread, from the world script's update (never inside a command): rebuilds the core's command map when a Lua
// command was registered or a reload changed the command list since the last call.
void applyCommandMapChange();
// World thread, from the world script's update after the deferred work: the bot API's once-per-tick snapshot, the
// event diff and the queued orders (B1: the snapshot and the events). A no-op while no provider is registered.
void pumpBots();

// E4: map states, called by the map scripts. scheduleMapState: OnCreate (under the core's map lock: it only records the
// map). beginMapUpdate: the map's OnUpdate, first thing, on the map's thread: builds and loads the state on the first
// update after a schedule and answers true then (the caller fires MAP_INITIALIZE). closeMapState: OnDestroy.
void scheduleMapState(uint32_t mapId, uint32_t instanceId);
bool beginMapUpdate(uint32_t mapId, uint32_t instanceId);
// True while a scheduled map waits for its first update (that hook replays its creation queue).
bool awaitingFirstUpdate(uint32_t mapId, uint32_t instanceId);
// E6: the map has a state of its own (MapStates on): its own update drains the queued core-hook events, else the world update.
bool mapOwnsState(uint32_t mapId, uint32_t instanceId);
// The map's own update: drains the queued core-hook events of this map when it owns a state (first thing after the creation
// replay, on the map's thread, outside every core loop).
size_t drainMapHooks(uint32_t mapId, uint32_t instanceId);
void closeMapState(uint32_t mapId, uint32_t instanceId);
size_t tickMapState(uint32_t mapId, uint32_t instanceId);
// The scheduler of the state set (never null while the module is on): StateSet::mapState/mapCount for the commands.
fl::StateSet* stateSet();

// The binder for every new state (src/bind).
void bindGame(struct lua_State* L);

}  // namespace flh
