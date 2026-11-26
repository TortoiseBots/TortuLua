#include "FakeHost.h"

#include "CreatureLookup.h"
#include "NearSearch.h"
#include "PlayerWrites.h"

#include "engine/GuidText.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <unistd.h>

namespace fltest
{
thread_local bool FakeHost::mapsLockHeld = false;

void FakeHost::notePoll(char const* op)
{
    if (std::this_thread::get_id() == worldThreadId)
        return;
    ++offWorldPolls;
    std::lock_guard<std::mutex> lock(_mutex);
    offWorldOps.emplace_back(op);
}

FakeHost::FakeHost() : worldThreadId(std::this_thread::get_id()), _t0(std::chrono::steady_clock::now())
{
    rules.setWorldThread();   // the test thread is the world thread
    schemas["world"] = standardSchema();
    schemas["characters"] = std::make_shared<fl::SchemaSnapshot>();
    schemas["characters"]->addTable("characters", {"guid", "name", "level"});
}

uint64_t FakeHost::nowMs()
{
    if (frozen)
    {
        uint64_t now = offset;
        offset += tickMs;
        return now;
    }
    auto d = std::chrono::steady_clock::now() - _t0;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()) + offset;
}

void FakeHost::log(fl::LogLevel level, std::string const& text)
{
    std::lock_guard<std::mutex> lock(_mutex);
    logs.emplace_back(level, text);
}

bool FakeHost::logContains(std::string const& needle) const
{
    for (auto const& l : logs)
        if (l.second.find(needle) != std::string::npos)
            return true;
    return false;
}

size_t FakeHost::logCount(fl::LogLevel level) const
{
    size_t n = 0;
    for (auto const& l : logs)
        n += l.first == level;
    return n;
}

std::string FakeHost::guidToString(fl::Guid const& g)
{
    return "Guid-" + std::to_string(g.hi) + "-" + std::to_string(g.lo);
}

fl::Guid FakeHost::makeGuid(uint64_t type, uint64_t low, uint32_t entry, uint16_t mapId)
{
    fl::Guid g;
    g.hi = (type << 58) | (static_cast<uint64_t>(mapId & 0x1FFF) << 44) | (static_cast<uint64_t>(entry) << 20);
    g.lo = low;
    return g;
}

fl::HandleData FakeHost::creature(uint64_t id, uint32_t map, uint32_t instance, uint32_t entry)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Creature;
    h.guid = makeGuid(kTypeCreature, id, entry, static_cast<uint16_t>(map));
    h.mapId = map;
    h.instanceId = instance;
    return h;
}

fl::HandleData FakeHost::pet(uint64_t id, uint32_t map, uint32_t instance, uint32_t entry)
{
    fl::HandleData h = creature(id, map, instance, entry);
    h.guid = makeGuid(kTypePet, id, entry, static_cast<uint16_t>(map));
    return h;
}

fl::HandleData FakeHost::player(uint64_t id, uint32_t map, uint32_t instance)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Player;
    h.guid = makeGuid(kTypePlayer, id);
    h.mapId = map;
    h.instanceId = instance;
    return h;
}

fl::HandleData FakeHost::gameObject(uint64_t id, uint32_t map, uint32_t instance, uint32_t entry)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::GameObject;
    h.guid = makeGuid(kTypeGameObject, id, entry, static_cast<uint16_t>(map));
    h.mapId = map;
    h.instanceId = instance;
    return h;
}

fl::HandleData FakeHost::corpse(uint64_t id, uint32_t map, uint32_t instance)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Corpse;
    h.guid = makeGuid(kTypeCorpse, id);
    h.mapId = map;
    h.instanceId = instance;
    return h;
}

fl::HandleData FakeHost::item(uint64_t id)
{
    fl::HandleData h;
    h.kind = fl::ObjKind::Item;
    h.guid = makeGuid(kTypeItem, id);
    return h;
}

void FakeHost::add(fl::HandleData const& h, fl::ObjectInfo const& info)
{
    _objects[keyOf(h)] = Obj{info, flh::MapKey{h.mapId, h.instanceId}};
}

void FakeHost::remove(fl::HandleData const& h)
{
    _objects.erase(keyOf(h));
}

void FakeHost::moveTo(fl::HandleData const& h, uint32_t map, uint32_t instance)
{
    auto it = _objects.find(keyOf(h));
    if (it != _objects.end())
        it->second.current = flh::MapKey{map, instance};
}

fl::ObjectInfo* FakeHost::find(fl::HandleData const& h)
{
    auto it = _objects.find(keyOf(h));
    if (it == _objects.end())
        return nullptr;
    // A creature or game object is in ONE map's store: a handle naming another map does not find it (the core's
    // Map::GetCreature on the handle's map). Players and items are global tables.
    if ((h.kind == fl::ObjKind::Creature || h.kind == fl::ObjKind::GameObject || h.kind == fl::ObjKind::Corpse) &&
        it->second.current != flh::MapKey{h.mapId, h.instanceId})
        return nullptr;
    if (h.kind == fl::ObjKind::Creature)
    {
        // The core's two stores: Map::GetCreature never holds a pet, Map::GetPet only a pet. The lookup is the module's own
        // rule (flh::findCreatureOrPet), so removing its fallback makes a pet unresolvable here too.
        struct Guid
        {
            bool pet;
            bool IsPet() const { return pet; }
        };
        struct StoreMap
        {
            fl::ObjectInfo* info;
            bool isPet;
            fl::ObjectInfo* GetCreature(Guid const&) { return isPet ? nullptr : info; }
            fl::ObjectInfo* GetPet(Guid const&) { return isPet ? info : nullptr; }
        };
        bool const isPet = (h.guid.hi >> 58) == kTypePet;
        StoreMap m{&it->second.info, isPet};
        if (!petFallback)
            return m.GetCreature(Guid{isPet});
        return flh::findCreatureOrPet<fl::ObjectInfo>(m, Guid{isPet});
    }
    return &it->second.info;
}

bool FakeHost::lookup(fl::HandleData const& h, fl::ObjectInfo* out)
{
    noteMapLock();
    ++lookupCalls;
    if (throwOnLookup)
        throw std::runtime_error("lookup exploded");
    fl::ObjectInfo* o = find(h);
    if (!o)
        return false;
    if (out)
        *out = *o;
    return true;
}

bool FakeHost::despawn(fl::HandleData const& h)
{
    if (!find(h))
        return false;
    remove(h);
    return true;
}

fl::Access FakeHost::checkAccess(fl::HandleData const& h)
{
    noteMapLock();
    if (denyAccess)
        return fl::Access::OtherMap;
    std::optional<flh::MapKey> declared, current;
    auto it = _objects.find(keyOf(h));
    switch (h.kind)
    {
        case fl::ObjKind::Player:
            declared = flh::MapKey{h.mapId, h.instanceId};
            if (it != _objects.end())
                current = it->second.current;
            break;
        case fl::ObjKind::Item:
            if (it != _objects.end())
                current = it->second.current;
            declared = current;
            break;
        case fl::ObjKind::Creature:
        case fl::ObjKind::GameObject:
        case fl::ObjKind::Corpse:
        case fl::ObjKind::BattleGround:
        case fl::ObjKind::Map:
            declared = current = flh::MapKey{h.mapId, h.instanceId};
            break;
        default: return fl::Access::Ok;
    }
    return rules.check(declared, current);
}

bool FakeHost::currentMap(fl::HandleData const& h, uint32_t& mapId, uint32_t& instanceId)
{
    if (h.kind == fl::ObjKind::Player || h.kind == fl::ObjKind::Item)
    {
        auto it = _objects.find(keyOf(h));
        if (it == _objects.end())
            return false;
        mapId = it->second.current.map;
        instanceId = it->second.current.instance;
        return true;
    }
    mapId = h.mapId;
    instanceId = h.instanceId;
    return true;
}

bool FakeHost::objectInWorld(fl::HandleData const& h)
{
    if (h.kind != fl::ObjKind::Creature && h.kind != fl::ObjKind::GameObject && h.kind != fl::ObjKind::Corpse &&
        h.kind != fl::ObjKind::Player)
        return true;
    // Like the real host (findObject): a creature/game object resolves only through its map's store(s), a player globally.
    if (h.kind == fl::ObjKind::Player)
        return _objects.count(keyOf(h)) != 0;
    return find(h) != nullptr;
}

std::shared_ptr<fl::SchemaSnapshot const> FakeHost::schema(std::string const& db)
{
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    ++schemaCalls;
    auto it = schemas.find(db);
    return it == schemas.end() ? nullptr : it->second;
}

std::string FakeHost::escape(std::string const&, std::string const& text)
{
    if (badEscape)
        return text;
    std::string o;
    for (char c : text)
    {
        if (c == '\\' || c == '\'' || c == '"')
            o += '\\';
        o += c;
    }
    return o;
}

bool FakeHost::selectAsync(std::string const&, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                           std::string&)
{
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    if (stalled)
        return true;   // accepted and never answered: the engine's own timeout must fire
    queries.push_back(sql);
    if (onSelect)
        onSelect();
    dbRequests.push_back(DbRequest{id, sql, maxRows, maxBytes});
    if (manualSelect)
        return true;   // the test completes it with answerSelect()
    if (throwOnSelect)
        throw std::runtime_error("select exploded");   // inside the Lua call: safe<> turns it into a Lua error
    if (failSelect)
    {
        _dbErrors[id] = "query_failed";
        dbRequests.pop_back();
        return true;
    }
    auto it = results.find(sql);
    fl::DbRows rows = it == results.end() ? defaultRows : it->second;
    if (bigCellBytes)
    {
        rows.columns = {"name"};
        rows.rows.assign(1, std::vector<fl::DbValue>{std::string(bigCellBytes, 'x')});
    }
    if (rows.rows.size() > maxRows)
        rows.rows.resize(maxRows);
    rows.maxBytes = maxBytes;
    if (bigCellBytes && maxBytes && bigCellBytes > maxBytes)
        rows.tooLarge = true;
    _dbResults[id] = std::move(rows);
    return true;
}

bool FakeHost::takeDbResult(uint64_t& id, fl::DbRows& out, std::string& error)
{
    notePoll("takeDbResult");
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    if (_dbResults.empty() && _dbErrors.empty())
        return false;
    if (!_dbResults.empty())
    {
        auto it = _dbResults.begin();
        id = it->first;
        out = std::move(it->second);
        _dbResults.erase(it);
    }
    else
    {
        auto it = _dbErrors.begin();
        id = it->first;
        error = it->second;
        _dbErrors.erase(it);
    }
    auto req = std::find_if(dbRequests.begin(), dbRequests.end(),
                            [&](DbRequest const& r) { return r.id == id; });
    if (req != dbRequests.end())
        dbRequests.erase(req);
    return true;
}

void FakeHost::cancelSelect(uint64_t id)
{
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    ++cancelled;
    _dbResults.erase(id);
    _dbErrors.erase(id);
    auto req = std::find_if(dbRequests.begin(), dbRequests.end(),
                            [&](DbRequest const& r) { return r.id == id; });
    if (req != dbRequests.end())
        dbRequests.erase(req);
}


namespace
{
int64_t unitIntAt(std::vector<fl::EventValue> const& a, size_t i, int64_t def = 0)
{
    if (i >= a.size())
        return def;
    if (auto const* v = std::get_if<int64_t>(&a[i].v))
        return *v;
    if (auto const* d = std::get_if<double>(&a[i].v))
        return static_cast<int64_t>(*d);
    return def;
}
double unitDoubleAt(std::vector<fl::EventValue> const& a, size_t i, double def = 0.0)
{
    if (i >= a.size())
        return def;
    if (auto const* d = std::get_if<double>(&a[i].v))
        return *d;
    if (auto const* v = std::get_if<int64_t>(&a[i].v))
        return static_cast<double>(*v);
    return def;
}
bool unitBoolAt(std::vector<fl::EventValue> const& a, size_t i, bool def = false)
{
    if (i >= a.size())
        return def;
    if (auto const* b = std::get_if<bool>(&a[i].v))
        return *b;
    return def;
}
std::string unitStringAt(std::vector<fl::EventValue> const& a, size_t i)
{
    if (i >= a.size())
        return std::string();
    if (auto const* s = std::get_if<std::string>(&a[i].v))
        return *s;
    return std::string();
}
bool unitHandleAt(std::vector<fl::EventValue> const& a, size_t i, fl::HandleData& out)
{
    if (i >= a.size())
        return false;
    if (auto const* h = std::get_if<fl::HandleData>(&a[i].v))
    {
        out = *h;
        return true;
    }
    return false;
}
}  // namespace

bool FakeHost::unitStateCall(fl::HandleData const& self, std::string const& op,
                             std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    // The documented Unit methods of the long-tail round 1. Anything else is still gameResults.
    static std::set<std::string> const ops = {
        "unit.getMaxPower", "unit.setMaxPower", "unit.getPowerType", "unit.setPowerType",
        "unit.getPowerPct", "unit.setMaxHealth", "unit.getStat", "unit.getBaseSpellPower", "unit.modifyPower",
        "unit.isStandState", "unit.setStandState", "unit.isMounted", "unit.mount", "unit.dismount", "unit.getMountId",
        "unit.isStopped", "unit.isCasting", "unit.isFullHealth", "unit.isRooted", "unit.hasUnitState",
        "unit.addUnitState", "unit.clearUnitState", "unit.isInWater", "unit.isUnderWater", "unit.isPvPFlagged",
        "unit.isAttackingPlayer", "unit.isCharmed", "unit.isVendor", "unit.isQuestGiver", "unit.isTrainer",
        "unit.isBanker", "unit.isInnkeeper", "unit.isGossip", "unit.isSpiritHealer", "unit.isServiceProvider",
        "unit.getSpeed", "unit.setSpeed", "unit.setRooted", "unit.setConfused", "unit.setFeared", "unit.setStunned",
        "unit.moveRandom", "unit.moveChase", "unit.moveConfused", "unit.moveFleeing", "unit.moveIdle", "unit.moveJump",
        "unit.moveExpire", "unit.moveClear", "unit.nearTeleport", "unit.getMovementType", "unit.setFacing",
        "unit.setFacingToObject", "unit.healthBelowPct", "unit.healthAbovePct", "unit.clearInCombat",
        "unit.setInCombatWith", "unit.dealHeal", "unit.stopSpellCast", "unit.interruptSpell", "unit.getCurrentSpell",
        "unit.getAura", "unit.getOwnedAura", "unit.removeAllAuras", "unit.removeArenaAuras", "unit.getCharmerGuid",
        "unit.getOwnerGuid", "unit.getControllerGuid", "unit.getCreatorGuid", "unit.getPetGuid", "unit.setOwnerGuid",
        "unit.getFriendlyUnitsInRange", "unit.getUnfriendlyUnitsInRange", "unit.getDisplayId", "unit.getNativeDisplayId",
        "unit.setNativeDisplayId", "unit.getCreatureType", "unit.getClassAsString", "unit.getRaceAsString",
        "unit.getClassMask", "unit.getRaceMask", "unit.sendChatMessageToPlayer", "unit.performEmote", "unit.emoteState",
        "unit.isMoving", "unit.isFlying", "unit.isVisible", "unit.setSheath", "unit.setWaterWalk", "unit.setSanctuary",
        "unit.deMorph", "unit.restoreDisplayId", "unit.restoreFaction",
        "unit.addFlatStatModifier", "unit.addPctStatModifier", "unit.canModifyStats", "unit.castSpellAoF",
        "unit.countPctFromCurHealth", "unit.countPctFromMaxHealth", "unit.disableMelee", "unit.getCharmGuid",
        "unit.getControllerGuids", "unit.getCritterGuid", "unit.getStandState", "unit.isArmorer", "unit.isAuctioneer",
        "unit.isBattleMaster", "unit.isDying", "unit.isGuildMaster", "unit.isInAccessiblePlaceFor",
        "unit.isSpiritGuide", "unit.isSpiritService", "unit.isTabardDesigner", "unit.isTaxi",
        "unit.removeBindSightAuras", "unit.removeCharmAuras", "unit.sendUnitEmote", "unit.setCanFly",
        "unit.setCreatorGuid", "unit.setCritterGuid", "unit.setFFA", "unit.setImmuneTo", "unit.setName",
        "unit.setPetGuid", "unit.setVisible"};
    if (!ops.count(op))
        return false;

    FakeUnit& u = unitState(self);
    auto action = [&u, &op, &args] { u.actions.emplace_back(op, args); };
    auto powerFor = [&u](std::vector<fl::EventValue> const& a, size_t i) -> int32_t
    {
        int64_t const p = unitIntAt(a, i, -1);
        return p >= 0 && p < 7 ? static_cast<int32_t>(p) : u.powerType;
    };

    if (op == "unit.getMaxPower")
        out.emplace_back(static_cast<int64_t>(u.powerTypeMax[powerFor(args, 0)]));
    else if (op == "unit.setMaxPower")
        u.powerTypeMax[powerFor(args, 0)] = static_cast<int32_t>(unitIntAt(args, 1));
    else if (op == "unit.getPowerType")
        out.emplace_back(static_cast<int64_t>(u.powerType));
    else if (op == "unit.setPowerType")
        u.powerType = static_cast<int32_t>(unitIntAt(args, 0));
    else if (op == "unit.getPowerPct")
    {
        int32_t const t = powerFor(args, 0);
        int32_t const max = u.powerTypeMax[t];
        out.emplace_back(max ? 100.0 * u.powerTypeValue[t] / max : 0.0);
    }
    else if (op == "unit.setMaxHealth")
        u.maxHealth = unitIntAt(args, 0);
    else if (op == "unit.getStat")
        out.emplace_back(u.stats[static_cast<uint32_t>(unitIntAt(args, 0))]);
    else if (op == "unit.getBaseSpellPower")
        out.emplace_back(u.baseSpellPower[static_cast<uint32_t>(unitIntAt(args, 0))]);
    else if (op == "unit.modifyPower")
        u.powerTypeValue[powerFor(args, 1)] += static_cast<int32_t>(unitIntAt(args, 0));
    else if (op == "unit.isStandState")
        out.emplace_back(u.standState == 0);
    else if (op == "unit.setStandState")
        u.standState = static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "unit.isMounted")
        out.emplace_back(u.mounted);
    else if (op == "unit.mount")
    {
        u.mounted = true;
        u.mountId = static_cast<uint32_t>(unitIntAt(args, 0));
        action();
    }
    else if (op == "unit.dismount")
    {
        u.mounted = false;
        action();
    }
    else if (op == "unit.getMountId")
        out.emplace_back(static_cast<int64_t>(u.mountId));
    else if (op == "unit.isStopped")
        out.emplace_back(u.stopped);
    else if (op == "unit.isCasting")
        out.emplace_back(u.casting);
    else if (op == "unit.isFullHealth")
        out.emplace_back(u.health == u.maxHealth);
    else if (op == "unit.isRooted")
        out.emplace_back(u.rooted);
    else if (op == "unit.hasUnitState")
        out.emplace_back((u.unitStates & static_cast<uint32_t>(unitIntAt(args, 0))) != 0);
    else if (op == "unit.addUnitState")
        u.unitStates |= static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "unit.clearUnitState")
        u.unitStates &= ~static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "unit.isInWater")
        out.emplace_back(u.inWater);
    else if (op == "unit.isUnderWater")
        out.emplace_back(u.underWater);
    else if (op == "unit.isPvPFlagged")
        out.emplace_back(u.pvp);
    else if (op == "unit.isAttackingPlayer")
        out.emplace_back(u.attackingPlayer);
    else if (op == "unit.isCharmed")
        out.emplace_back(u.charmed);
    else if (op == "unit.isVendor")
        out.emplace_back(u.vendor);
    else if (op == "unit.isQuestGiver")
        out.emplace_back(u.questGiver);
    else if (op == "unit.isTrainer")
        out.emplace_back(u.trainer);
    else if (op == "unit.isBanker")
        out.emplace_back(u.banker);
    else if (op == "unit.isInnkeeper")
        out.emplace_back(u.innkeeper);
    else if (op == "unit.isGossip")
        out.emplace_back(u.gossip);
    else if (op == "unit.isSpiritHealer")
        out.emplace_back(u.spiritHealer);
    else if (op == "unit.isServiceProvider")
        out.emplace_back(u.serviceProvider);
    else if (op == "unit.getSpeed")
        out.emplace_back(u.speed[static_cast<uint32_t>(unitIntAt(args, 0))]);
    else if (op == "unit.setSpeed")
    {
        uint32_t const type = static_cast<uint32_t>(unitIntAt(args, 0));
        u.speedRate[type] = unitDoubleAt(args, 1);
        if (unitBoolAt(args, 2, false))
            u.forcedSpeed.push_back(type);
        action();
    }
    else if (op == "unit.setRooted" || op == "unit.setConfused" || op == "unit.setFeared" || op == "unit.setStunned")
    {
        u.controlled[op] = unitBoolAt(args, 0, true);
        if (op == "unit.setRooted")
            u.rooted = unitBoolAt(args, 0, true);
        action();
    }
    else if (op == "unit.moveRandom" || op == "unit.moveChase" || op == "unit.moveConfused" ||
             op == "unit.moveFleeing" || op == "unit.moveIdle" || op == "unit.moveJump" || op == "unit.moveExpire" ||
             op == "unit.moveClear")
        action();
    else if (op == "unit.nearTeleport")
    {
        u.teleported = true;
        u.teleportX = unitDoubleAt(args, 0);
        u.teleportY = unitDoubleAt(args, 1);
        u.teleportZ = unitDoubleAt(args, 2);
        u.teleportO = unitDoubleAt(args, 3);
    }
    else if (op == "unit.getMovementType")
        out.emplace_back(static_cast<int64_t>(u.movementType));
    else if (op == "unit.setFacing")
        u.facing = unitDoubleAt(args, 0);
    else if (op == "unit.setFacingToObject")
    {
        fl::HandleData target;
        if (unitHandleAt(args, 0, target))
        {
            u.facingObject = true;
            u.facingObjectLow = target.guid.lo;
        }
    }
    else if (op == "unit.healthBelowPct")
        out.emplace_back(100.0 * static_cast<double>(u.health) / static_cast<double>(u.maxHealth) <
                         unitDoubleAt(args, 0));
    else if (op == "unit.healthAbovePct")
        out.emplace_back(100.0 * static_cast<double>(u.health) / static_cast<double>(u.maxHealth) >
                         unitDoubleAt(args, 0));
    else if (op == "unit.clearInCombat")
        u.inCombat = false;
    else if (op == "unit.setInCombatWith")
    {
        fl::HandleData enemy;
        if (unitHandleAt(args, 0, enemy))
        {
            u.inCombat = true;
            u.combatWithLow = enemy.guid.lo;
        }
    }
    else if (op == "unit.dealHeal")
    {
        fl::HandleData target;
        FakeUnit::Heal heal;
        if (unitHandleAt(args, 0, target))
            heal.targetLow = target.guid.lo;
        heal.spell = static_cast<uint32_t>(unitIntAt(args, 1));
        heal.amount = static_cast<uint32_t>(unitIntAt(args, 2));
        heal.critical = unitBoolAt(args, 3, false);
        u.heals.push_back(heal);
        if (target.kind == fl::ObjKind::Player || target.kind == fl::ObjKind::Creature)
            unitState(target).health += heal.amount;
    }
    else if (op == "unit.stopSpellCast")
    {
        u.hasStoppedSpell = true;
        u.stoppedSpell = static_cast<uint32_t>(unitIntAt(args, 0));
    }
    else if (op == "unit.interruptSpell")
    {
        u.interruptedType = static_cast<int32_t>(unitIntAt(args, 0));
        u.interruptDelayed = unitBoolAt(args, 1, true);
    }
    else if (op == "unit.getCurrentSpell")
    {
        if (u.hasCurrentSpell)
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::Spell;
            h.mapId = u.currentSpellId;
            out.emplace_back(h);
        }
    }
    else if (op == "unit.getAura" || op == "unit.getOwnedAura")
    {
        uint32_t const spell = static_cast<uint32_t>(unitIntAt(args, 0));
        bool const found = op == "unit.getAura" ? (u.auraSpell != 0 && spell == u.auraSpell)
                                                : (u.ownedAuraSpell != 0 && spell == u.ownedAuraSpell);
        if (found)
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::Aura;
            h.mapId = spell;
            h.instanceId = 1;
            h.guid.hi = 0;
            h.guid.lo = 77;
            out.emplace_back(h);
        }
    }
    else if (op == "unit.removeAllAuras")
        ++u.removeAllAuras;
    else if (op == "unit.removeArenaAuras")
        ++u.removeArenaAuras;
    else if (op == "unit.getCharmerGuid")
        out.emplace_back(u.charmerGuid);
    else if (op == "unit.getOwnerGuid")
        out.emplace_back(u.ownerGuid);
    else if (op == "unit.getControllerGuid")
        out.emplace_back(u.controllerGuid);
    else if (op == "unit.getCreatorGuid")
        out.emplace_back(u.creatorGuid);
    else if (op == "unit.getPetGuid")
        out.emplace_back(u.petGuid);
    else if (op == "unit.setOwnerGuid")
    {
        u.ownerGuid = unitStringAt(args, 0);
        action();
    }
    else if (op == "unit.getFriendlyUnitsInRange" || op == "unit.getUnfriendlyUnitsInRange")
    {
        u.listRange = unitDoubleAt(args, 0, 533.333);
        auto const& list = op == "unit.getFriendlyUnitsInRange" ? u.friendlyUnits : u.unfriendlyUnits;
        for (fl::HandleData const& h : list)
            out.emplace_back(h);
    }
    else if (op == "unit.getDisplayId")
        out.emplace_back(static_cast<int64_t>(u.displayId));
    else if (op == "unit.getNativeDisplayId")
        out.emplace_back(static_cast<int64_t>(u.nativeDisplayId));
    else if (op == "unit.setNativeDisplayId")
    {
        // the host refuses an id the display tables do not hold (the core would abort later) and answers false;
        // here ids 1..899999 stand for the known ones
        int64_t const id = unitIntAt(args, 0);
        if (id < 1 || id >= 900000)
            out.emplace_back(false);
        else
        {
            u.nativeDisplayId = static_cast<uint32_t>(id);
            u.displayId = u.nativeDisplayId;   // the core sets both (SetDisplayId(id, setNative = true))
        }
    }
    else if (op == "unit.getCreatureType")
        out.emplace_back(static_cast<int64_t>(u.creatureType));
    else if (op == "unit.getClassAsString")
    {
        u.classLocale = static_cast<int32_t>(unitIntAt(args, 0));
        if (!u.classAsString.empty())
            out.emplace_back(u.classAsString);
    }
    else if (op == "unit.getRaceAsString")
    {
        u.raceLocale = static_cast<int32_t>(unitIntAt(args, 0));
        if (!u.raceAsString.empty())
            out.emplace_back(u.raceAsString);
    }
    else if (op == "unit.getClassMask")
        out.emplace_back(static_cast<int64_t>(u.classMask));
    else if (op == "unit.getRaceMask")
        out.emplace_back(static_cast<int64_t>(u.raceMask));
    else if (op == "unit.sendChatMessageToPlayer")
    {
        FakeUnit::Chat chat;
        chat.type = static_cast<uint32_t>(unitIntAt(args, 0));
        chat.lang = static_cast<uint32_t>(unitIntAt(args, 1));
        chat.msg = unitStringAt(args, 2);
        fl::HandleData target;
        if (unitHandleAt(args, 3, target))
            chat.targetLow = target.guid.lo;
        u.chats.push_back(chat);
    }
    else if (op == "unit.performEmote")
        u.emote = static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "unit.emoteState")
        u.emoteState = static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "unit.isMoving")
        out.emplace_back(u.moving);
    else if (op == "unit.isFlying")
        out.emplace_back(u.flying);
    else if (op == "unit.isVisible")
        out.emplace_back(u.visible);
    else if (op == "unit.setSheath")
        u.sheath = static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "unit.setWaterWalk")
        u.waterWalk = unitBoolAt(args, 0, true);
    else if (op == "unit.setSanctuary")
        u.sanctuary = unitBoolAt(args, 0, true);
    else if (op == "unit.deMorph")
        ++u.deMorphs;
    else if (op == "unit.restoreDisplayId")
        ++u.restoreDisplayId;
    else if (op == "unit.restoreFaction")
        ++u.restoreFaction;
    else if (op == "unit.addFlatStatModifier")
    {
        u.statMods.push_back(FakeUnit::StatMod{static_cast<uint32_t>(unitIntAt(args, 0)),
                                               static_cast<uint32_t>(unitIntAt(args, 1)), unitDoubleAt(args, 2),
                                               unitBoolAt(args, 3, true), false});
    }
    else if (op == "unit.addPctStatModifier")
    {
        u.statMods.push_back(FakeUnit::StatMod{static_cast<uint32_t>(unitIntAt(args, 0)),
                                               static_cast<uint32_t>(unitIntAt(args, 1)), unitDoubleAt(args, 2),
                                               true, true});
    }
    else if (op == "unit.canModifyStats")
        out.emplace_back(u.canModifyStats);
    else if (op == "unit.castSpellAoF")
    {
        u.aofCasts.push_back(FakeUnit::AoFCast{unitDoubleAt(args, 0), unitDoubleAt(args, 1), unitDoubleAt(args, 2),
                                               static_cast<uint32_t>(unitIntAt(args, 3)), unitBoolAt(args, 4, true)});
    }
    else if (op == "unit.countPctFromCurHealth")
    {
        double const pct = unitDoubleAt(args, 0, 0.0);
        out.emplace_back(static_cast<int64_t>(static_cast<double>(u.health) * pct / 100.0));
    }
    else if (op == "unit.countPctFromMaxHealth")
    {
        double const pct = unitDoubleAt(args, 0, 0.0);
        out.emplace_back(static_cast<int64_t>(static_cast<double>(u.maxHealth) * pct / 100.0));
    }
    else if (op == "unit.disableMelee")
    {
        if (self.kind != fl::ObjKind::Creature)
        {
            out.emplace_back(false);
            return true;
        }
        u.meleeDisabled = unitBoolAt(args, 0, true);
        out.emplace_back(true);
    }
    else if (op == "unit.getCharmGuid")
        out.emplace_back(u.charmGuid);
    else if (op == "unit.getControllerGuids")
        out.emplace_back(u.controllerGuids);
    else if (op == "unit.getCritterGuid")
        out.emplace_back(u.critterGuid);
    else if (op == "unit.getStandState")
        out.emplace_back(static_cast<int64_t>(u.standState));
    else if (op == "unit.isArmorer")
        out.emplace_back(u.armorer);
    else if (op == "unit.isAuctioneer")
        out.emplace_back(u.auctioneer);
    else if (op == "unit.isBattleMaster")
        out.emplace_back(u.battleMaster);
    else if (op == "unit.isDying")
        out.emplace_back(u.dying);
    else if (op == "unit.isGuildMaster")
        out.emplace_back(u.guildMaster);
    else if (op == "unit.isInAccessiblePlaceFor")
    {
        fl::HandleData target;
        if (unitHandleAt(args, 0, target))
            u.accessibleObjLow = target.guid.lo;
        u.accessibleRadius = unitDoubleAt(args, 1, 0.0);
        out.emplace_back(u.accessiblePlace);
    }
    else if (op == "unit.isSpiritGuide")
        out.emplace_back(u.spiritGuide);
    else if (op == "unit.isSpiritService")
        out.emplace_back(u.spiritService);
    else if (op == "unit.isTabardDesigner")
        out.emplace_back(u.tabardDesigner);
    else if (op == "unit.isTaxi")
        out.emplace_back(u.taxi);
    else if (op == "unit.removeBindSightAuras")
        ++u.removeBindSightAuras;
    else if (op == "unit.removeCharmAuras")
        ++u.removeCharmAuras;
    else if (op == "unit.sendUnitEmote")
    {
        FakeUnit::EmoteMsg e;
        e.msg = unitStringAt(args, 0);
        fl::HandleData receiver;
        if (unitHandleAt(args, 1, receiver))
            e.receiverLow = receiver.guid.lo;
        e.bossEmote = unitBoolAt(args, 2, false);
        u.unitEmotes.push_back(std::move(e));
    }
    else if (op == "unit.setCanFly")
        u.canFly = unitBoolAt(args, 0, true);
    else if (op == "unit.setCreatorGuid")
        u.creatorGuid = unitStringAt(args, 0);
    else if (op == "unit.setCritterGuid")
        u.critterGuid = unitStringAt(args, 0);
    else if (op == "unit.setFFA")
    {
        bool const on = unitBoolAt(args, 0, true);
        u.ffa = on;
        if (self.kind == fl::ObjKind::Player)
            playerState(self).ffa = on;
    }
    else if (op == "unit.setImmuneTo")
    {
        uint32_t const mech = static_cast<uint32_t>(unitIntAt(args, 0));
        if (unitBoolAt(args, 1, true))
            u.immuneMechanics.insert(mech);
        else
            u.immuneMechanics.erase(mech);
    }
    else if (op == "unit.setName")
    {
        auto cit = creatureStates.find({self.guid.hi, self.guid.lo});
        bool const isPet = (self.guid.hi >> 58) == kTypePet || (cit != creatureStates.end() && cit->second.isPet);
        if (self.kind != fl::ObjKind::Creature || isPet)
        {
            out.emplace_back(false);
            return true;
        }
        u.name = unitStringAt(args, 0);
        out.emplace_back(true);
    }
    else if (op == "unit.setPetGuid")
        u.petGuid = unitStringAt(args, 0);
    else if (op == "unit.setVisible")
        u.visible = unitBoolAt(args, 0, true);
    return true;
}


bool FakeHost::creatureStateCall(fl::HandleData const& self, std::string const& op,
                                 std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    // The documented Creature methods of the long-tail round 1. Anything else is still gameResults.
    static std::set<std::string> const ops = {
        "creature.attackStart", "creature.selectVictim", "creature.callAssistance", "creature.callForHelp",
        "creature.fleeToGetAssistance", "creature.setNoCallAssistance", "creature.setNoSearchAssistance",
        "creature.hasSearchedAssistance", "creature.isInEvadeMode", "creature.setInCombatWithZone",
        "creature.getAggroRange", "creature.getAttackDistance", "creature.canAssistTo", "creature.canStartAttack",
        "creature.isTappedBy", "creature.hasLootRecipient", "creature.getLootRecipient",
        "creature.getLootRecipientGroup", "creature.addThreat", "creature.getThreat", "creature.clearThreat",
        "creature.clearThreatList", "creature.resetAllThreat", "creature.fixateTarget", "creature.clearFixate",
        "creature.getAITargets", "creature.getAITargetsCount", "creature.isCivilian", "creature.isGuard",
        "creature.isRacialLeader", "creature.isTrigger", "creature.isDungeonBoss",
        "creature.isRegeneratingHealth", "creature.setRegeneratingHealth", "creature.canFly", "creature.canSwim",
        "creature.setHover", "creature.setDisableGravity", "creature.isReputationGainDisabled",
        "creature.setDisableReputationGain", "creature.getShieldBlockValue", "creature.setWalk",
        "creature.getWaypointPath", "creature.getCurrentWaypointId", "creature.moveWaypoint",
        "creature.getDefaultMovementType", "creature.setDefaultMovementType", "creature.getWanderRadius",
        "creature.setWanderRadius", "creature.setHomePosition", "creature.getRespawnDelay",
        "creature.getCorpseDelay", "creature.removeCorpse", "creature.setDeathState",
        "creature.setEquipmentSlots", "creature.getCreatureFamily", "creature.getNPCFlags",
        "creature.setNPCFlags", "creature.getExtraFlags", "creature.getRank", "creature.getDBTableGUIDLow",
        "creature.getAIName", "creature.getScriptId", "creature.updateEntry", "creature.hasSpell",
        "creature.hasSpellCooldown", "creature.hasCategoryCooldown", "creature.getSpellCooldownDelay",
        "creature.getLootMode", "creature.hasLootMode", "creature.setLootMode", "creature.addLootMode",
        "creature.removeLootMode", "creature.resetLootMode", "creature.hasQuest"};
    if (!ops.count(op))
        return false;

    FakeCreature& c = creatureState(self);
    auto action = [&c, &op, &args] { c.actions.emplace_back(op, args); };
    auto playerHandle = [](uint64_t low)
    {
        fl::HandleData h;
        h.kind = fl::ObjKind::Player;
        h.guid = FakeHost::makeGuid(FakeHost::kTypePlayer, low);
        return h;
    };

    if (op == "creature.attackStart")
    {
        if (!c.hasAi)
        {
            out.emplace_back(false);   // the binding raises "the creature has no AI"
            return true;
        }
        fl::HandleData target;
        if (unitHandleAt(args, 0, target))
            c.attackStartTargetLow = target.guid.lo;
        ++c.attackStarts;
    }
    else if (op == "creature.selectVictim")
    {
        if (!c.hasAi)
        {
            out.emplace_back(false);   // the binding raises "the creature has no AI"
            return true;
        }
        ++c.selectVictims;
        if (c.victimLow)
            out.emplace_back(fl::EventValue(playerHandle(c.victimLow)));
    }
    else if (op == "creature.callAssistance")
        ++c.callAssistances;
    else if (op == "creature.callForHelp")
    {
        c.callForHelpRadius = unitDoubleAt(args, 0);
        ++c.callForHelps;
    }
    else if (op == "creature.fleeToGetAssistance")
    {
        if (c.isPet)
        {
            out.emplace_back(false);
            return true;
        }
        ++c.flees;
    }
    else if (op == "creature.setNoCallAssistance")
        c.noCallAssistance = unitBoolAt(args, 0, true);
    else if (op == "creature.setNoSearchAssistance")
        c.noSearchAssistance = unitBoolAt(args, 0, true);
    else if (op == "creature.hasSearchedAssistance")
        out.emplace_back(c.searchedAssistance);
    else if (op == "creature.isInEvadeMode")
        out.emplace_back(c.evadeMode);
    else if (op == "creature.setInCombatWithZone")
    {
        if (c.isPet)
        {
            out.emplace_back(false);
            return true;
        }
        ++c.zoneCombats;
    }
    else if (op == "creature.getAggroRange" || op == "creature.getAttackDistance")
    {
        fl::HandleData target;
        if (unitHandleAt(args, 0, target))
            c.rangeTargetLow = target.guid.lo;
        out.emplace_back(op == "creature.getAggroRange" ? c.aggroRange : c.attackDistance);
    }
    else if (op == "creature.canAssistTo")
    {
        fl::HandleData friendArg, enemyArg;
        if (unitHandleAt(args, 0, friendArg))
            c.assistFriendLow = friendArg.guid.lo;
        if (unitHandleAt(args, 1, enemyArg))
            c.assistEnemyLow = enemyArg.guid.lo;
        c.assistCheckFaction = unitBoolAt(args, 2, true);
        out.emplace_back(c.canAssist);
    }
    else if (op == "creature.canStartAttack")
    {
        fl::HandleData target;
        if (unitHandleAt(args, 0, target))
            c.canStartAttackTargetLow = target.guid.lo;
        c.canStartAttackForce = unitBoolAt(args, 1, true);
        out.emplace_back(c.canStartAttack);
    }
    else if (op == "creature.isTappedBy")
    {
        fl::HandleData player;
        if (unitHandleAt(args, 0, player))
            c.lastTappedLow = player.guid.lo;
        out.emplace_back(c.tapped);
    }
    else if (op == "creature.hasLootRecipient")
        out.emplace_back(!c.tapList.empty());
    else if (op == "creature.getLootRecipient")
    {
        if (!c.tapList.empty())
            out.emplace_back(fl::EventValue(playerHandle(c.tapList.front())));
    }
    else if (op == "creature.getLootRecipientGroup")
    {
        if (!c.tapList.empty() && !c.tapGroups.empty() && c.tapGroups.front())
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::Group;
            h.guid = FakeHost::makeGuid(1, c.tapGroups.front());
            out.emplace_back(fl::EventValue(h));
        }
    }
    else if (op == "creature.addThreat")
    {
        uint32_t const schoolMask = static_cast<uint32_t>(unitIntAt(args, 2));
        uint32_t const spell = static_cast<uint32_t>(unitIntAt(args, 3));
        if ((spell && !c.serverSpells.count(spell)) || (!spell && schoolMask))
        {
            out.emplace_back(false);
            return true;
        }
        fl::HandleData victim;
        if (unitHandleAt(args, 0, victim))
            c.threat[victim.guid.lo] += unitDoubleAt(args, 1);
    }
    else if (op == "creature.getThreat")
    {
        fl::HandleData victim;
        if (unitHandleAt(args, 0, victim))
            out.emplace_back(c.threat[victim.guid.lo]);
    }
    else if (op == "creature.clearThreat")
    {
        fl::HandleData victim;
        if (unitHandleAt(args, 0, victim))
            c.threat.erase(victim.guid.lo);
    }
    else if (op == "creature.clearThreatList")
    {
        ++c.clearThreatLists;
        c.threat.clear();
    }
    else if (op == "creature.resetAllThreat")
    {
        ++c.resetAllThreats;
        for (auto& entry : c.threat)
            entry.second = 0;
    }
    else if (op == "creature.fixateTarget")
    {
        fl::HandleData target;
        if (unitHandleAt(args, 0, target))
            c.fixateLow = target.guid.lo;
    }
    else if (op == "creature.clearFixate")
    {
        ++c.clearFixates;
        c.fixateLow = 0;
    }
    else if (op == "creature.getAITargets")
    {
        for (uint64_t low : c.threatOrder)
            out.emplace_back(fl::EventValue(playerHandle(low)));
    }
    else if (op == "creature.getAITargetsCount")
        out.emplace_back(static_cast<int64_t>(c.threatOrder.size()));
    else if (op == "creature.isCivilian")
        out.emplace_back(c.civilian);
    else if (op == "creature.isGuard")
        out.emplace_back(c.guard);
    else if (op == "creature.isRacialLeader")
        out.emplace_back(c.racialLeader);
    else if (op == "creature.isTrigger")
        out.emplace_back(c.trigger);
    else if (op == "creature.isDungeonBoss")
        out.emplace_back(c.dungeonBoss);
    else if (op == "creature.isRegeneratingHealth")
        out.emplace_back(c.regenerating);
    else if (op == "creature.setRegeneratingHealth")
    {
        bool const enable = unitBoolAt(args, 0, true);
        if (enable && c.regenForbidden)
        {
            out.emplace_back(false);   // the getter would not read the value back
            return true;
        }
        c.regenerating = enable;
    }
    else if (op == "creature.canFly")
        out.emplace_back(c.canFly);
    else if (op == "creature.canSwim")
        out.emplace_back(c.canSwim);
    else if (op == "creature.setHover")
        c.hover = unitBoolAt(args, 0, true);
    else if (op == "creature.setDisableGravity")
        c.disableGravity = unitBoolAt(args, 0, false);
    else if (op == "creature.isReputationGainDisabled")
        out.emplace_back(c.reputationDisabled);
    else if (op == "creature.setDisableReputationGain")
        c.reputationDisabled = unitBoolAt(args, 0, true);
    else if (op == "creature.getShieldBlockValue")
        out.emplace_back(c.shieldBlockValue);
    else if (op == "creature.setWalk")
        c.walk = unitBoolAt(args, 0, true);
    else if (op == "creature.getWaypointPath")
        out.emplace_back(static_cast<int64_t>(c.waypointPath));
    else if (op == "creature.getCurrentWaypointId")
        out.emplace_back(static_cast<int64_t>(c.currentWaypoint));
    else if (op == "creature.moveWaypoint")
    {
        if (!c.waypointPath || !c.knownPaths.count(c.waypointPath))
        {
            out.emplace_back(false);
            return true;
        }
        ++c.moveWaypoints;
    }
    else if (op == "creature.getDefaultMovementType")
        out.emplace_back(static_cast<int64_t>(c.defaultMovementType));
    else if (op == "creature.setDefaultMovementType")
    {
        if (c.isPet)
        {
            out.emplace_back(false);
            return true;
        }
        c.defaultMovementType = static_cast<uint32_t>(unitIntAt(args, 0));
    }
    else if (op == "creature.getWanderRadius")
        out.emplace_back(c.wanderRadius);
    else if (op == "creature.setWanderRadius")
        c.wanderRadius = unitDoubleAt(args, 0);
    else if (op == "creature.setHomePosition")
    {
        c.homeX = unitDoubleAt(args, 0);
        c.homeY = unitDoubleAt(args, 1);
        c.homeZ = unitDoubleAt(args, 2);
        c.homeO = unitDoubleAt(args, 3);
    }
    else if (op == "creature.getRespawnDelay")
        out.emplace_back(static_cast<int64_t>(c.respawnDelay));
    else if (op == "creature.getCorpseDelay")
        out.emplace_back(static_cast<int64_t>(c.corpseDelay));
    else if (op == "creature.removeCorpse")
        ++c.corpseRemovals;
    else if (op == "creature.setDeathState")
    {
        if (unitIntAt(args, 0) == 0 && c.healthZero)
        {
            out.emplace_back(false);
            return true;
        }
        // CORPSE / DEAD on a living creature (alive is state 0 or 4 here) would leave it at full health
        if ((unitIntAt(args, 0) == 2 || unitIntAt(args, 0) == 3) && (c.deathState == 0 || c.deathState == 4))
        {
            out.emplace_back(false);
            return true;
        }
        c.deathState = static_cast<uint32_t>(unitIntAt(args, 0));
    }
    else if (op == "creature.setEquipmentSlots")
    {
        for (size_t i = 0; i < 3; ++i)
            if (uint32_t const entry = static_cast<uint32_t>(unitIntAt(args, i)); entry && !c.knownItems.count(entry))
            {
                out.emplace_back(false);
                return true;
            }
        c.equipMain = static_cast<uint32_t>(unitIntAt(args, 0));
        c.equipOff = static_cast<uint32_t>(unitIntAt(args, 1));
        c.equipRanged = static_cast<uint32_t>(unitIntAt(args, 2));
    }
    else if (op == "creature.getCreatureFamily" || op == "creature.getExtraFlags" || op == "creature.getRank")
    {
        if (!c.hasTemplate)
        {
            out.emplace_back(false);
            return true;
        }
        if (op == "creature.getCreatureFamily")
            out.emplace_back(static_cast<int64_t>(c.family));
        else if (op == "creature.getExtraFlags")
            out.emplace_back(static_cast<int64_t>(c.extraFlags));
        else
            out.emplace_back(static_cast<int64_t>(c.rank));
    }
    else if (op == "creature.getNPCFlags")
        out.emplace_back(static_cast<int64_t>(c.npcFlags));
    else if (op == "creature.setNPCFlags")
        c.npcFlags = static_cast<uint64_t>(unitIntAt(args, 0));
    else if (op == "creature.getDBTableGUIDLow")
        out.emplace_back(static_cast<int64_t>(c.dbGuidLow));
    else if (op == "creature.getAIName")
        out.emplace_back(c.aiName);
    else if (op == "creature.getScriptId")
        out.emplace_back(static_cast<int64_t>(c.scriptId));
    else if (op == "creature.updateEntry")
    {
        uint32_t const entry = static_cast<uint32_t>(unitIntAt(args, 0));
        uint32_t const dataLow = static_cast<uint32_t>(unitIntAt(args, 1));
        if (!c.knownEntries.count(entry) || c.noModelEntries.count(entry) || c.isPet ||
            (dataLow && !c.knownDataLow.count(dataLow)))
        {
            out.emplace_back(false);
            return true;
        }
        c.entry = entry;
    }
    else if (op == "creature.hasSpell")
        out.emplace_back(c.spells.count(static_cast<uint32_t>(unitIntAt(args, 0))) != 0);
    else if (op == "creature.hasSpellCooldown")
    {
        // the host answers 1 / 0 / -1 (an unknown spell: the core's lookup by id asserts)
        uint32_t const spell = static_cast<uint32_t>(unitIntAt(args, 0));
        if (!c.serverSpells.count(spell))
            out.emplace_back(static_cast<int64_t>(-1));
        else
            out.emplace_back(static_cast<int64_t>(c.spellCooldowns.count(spell) ? 1 : 0));
    }
    else if (op == "creature.hasCategoryCooldown")
    {
        // the host answers 1 / 0 / -1 (the script's bool is pushed by the binding)
        uint32_t const spell = static_cast<uint32_t>(unitIntAt(args, 0));
        if (!c.serverSpells.count(spell))
            out.emplace_back(static_cast<int64_t>(-1));
        else
            out.emplace_back(static_cast<int64_t>(c.categoryCooldowns.count(spell) ? 1 : 0));
    }
    else if (op == "creature.getSpellCooldownDelay")
    {
        uint32_t const spell = static_cast<uint32_t>(unitIntAt(args, 0));
        if (!c.serverSpells.count(spell))
            out.emplace_back(false);
        else
            out.emplace_back(c.spellCooldownDelay[spell]);
    }
    else if (op == "creature.getLootMode")
        out.emplace_back(static_cast<int64_t>(c.lootMode));
    else if (op == "creature.hasLootMode")
        out.emplace_back((c.lootMode & static_cast<uint32_t>(unitIntAt(args, 0))) != 0);
    else if (op == "creature.setLootMode")
        c.lootMode = static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "creature.addLootMode")
        c.lootMode |= static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "creature.removeLootMode")
        c.lootMode &= ~static_cast<uint32_t>(unitIntAt(args, 0));
    else if (op == "creature.resetLootMode")
        c.lootMode = 1;
    else  // creature.hasQuest
        out.emplace_back(c.quests.count(static_cast<uint32_t>(unitIntAt(args, 0))) != 0);
    (void)action;
    return true;
}

bool FakeHost::itemStateCall(fl::HandleData const& self, std::string const& op,
                             std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    // The documented Item methods of the long-tail round 1. Anything else is still gameResults.
    static std::set<std::string> const ops = {
        "item.getName", "item.getItemId", "item.getItemLevel", "item.getRequiredLevel", "item.getClass",
        "item.getSubClass", "item.getInventoryType", "item.getQuality", "item.getDisplayId", "item.getBuyPrice",
        "item.getSellPrice", "item.getBuyCount", "item.getMaxStackCount", "item.getStatsCount", "item.getItemSet",
        "item.getAllowableClass", "item.getAllowableRace", "item.getArmor", "item.getDurability",
        "item.getMaxDurability", "item.getFlags", "item.getFlags2", "item.getSpellId", "item.getSpellTrigger",
        "item.getStatInfo", "item.getDamageInfo", "item.isBag", "item.isSoulBound", "item.isBoundAccountWide",
        "item.isBoundByEnchant", "item.isNotBoundToPlayer", "item.isLocked", "item.isBroken",
        "item.isCurrencyToken", "item.isNotEmptyBag", "item.isInBag", "item.isInTrade", "item.isPotion",
        "item.isConjuredConsumable", "item.isRefundExpired", "item.canBeTraded", "item.hasQuest",
        "item.getOwnerGuid", "item.setBinding", "item.getEnchantmentId", "item.setEnchantment",
        "item.clearEnchantment", "item.getBagSize", "item.getExtraFlags", "item.getSpeed"};
    if (!ops.count(op))
        return false;

    FakeItemState& it = itemState(self);
    auto templated = [&it, &out]()
    {
        if (it.hasTemplate)
            return true;
        out.emplace_back(false);
        return false;
    };

    if (op == "item.getName")
    {
        if (!templated())
            return true;
        out.emplace_back(it.name);
    }
    else if (op == "item.getItemId")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.entry));
    }
    else if (op == "item.getItemLevel")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.itemLevel));
    }
    else if (op == "item.getRequiredLevel")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.requiredLevel));
    }
    else if (op == "item.getClass")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.itemClass));
    }
    else if (op == "item.getSubClass")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.subClass));
    }
    else if (op == "item.getInventoryType")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.inventoryType));
    }
    else if (op == "item.getQuality")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.quality));
    }
    else if (op == "item.getDisplayId")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.displayId));
    }
    else if (op == "item.getBuyPrice")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.buyPrice));
    }
    else if (op == "item.getSellPrice")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.sellPrice));
    }
    else if (op == "item.getBuyCount")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.buyCount));
    }
    else if (op == "item.getMaxStackCount")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.maxStack));
    }
    else if (op == "item.getStatsCount")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.statsCount));
    }
    else if (op == "item.getItemSet")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.itemSet));
    }
    else if (op == "item.getAllowableClass")
    {
        if (!templated())
            return true;
        out.emplace_back(it.allowableClass);
    }
    else if (op == "item.getAllowableRace")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.allowableRace));
    }
    else if (op == "item.getArmor")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.armor));
    }
    else if (op == "item.getFlags")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.flags));
    }
    else if (op == "item.getFlags2")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.flags2));
    }
    else if (op == "item.getDurability")
        out.emplace_back(static_cast<int64_t>(it.durability));
    else if (op == "item.getMaxDurability")
        out.emplace_back(static_cast<int64_t>(it.maxDurability));
    else if (op == "item.getSpellId" || op == "item.getSpellTrigger")
    {
        if (!templated())
            return true;
        uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
        if (index >= it.spellCount)
        {
            out.emplace_back(false);
            return true;
        }
        out.emplace_back(op == "item.getSpellId" ? static_cast<int64_t>(it.spellIds[index])
                                                 : static_cast<int64_t>(it.spellTriggers[index]));
    }
    else if (op == "item.getStatInfo")
    {
        uint32_t const slot = static_cast<uint32_t>(unitIntAt(args, 0));
        if (slot >= 10 || !it.hasOwner)
        {
            out.emplace_back(false);
            return true;
        }
        out.emplace_back(it.statValue);
        out.emplace_back(static_cast<int64_t>(it.statType));
    }
    else if (op == "item.getDamageInfo")
    {
        if (!templated())
            return true;
        if (it.damageRowMissing)   // the item level has no row in the core's damage tables
        {
            out.emplace_back(false);
            return true;
        }
        // one damage pair in the core's data: the second documented slot answers zeros
        bool const first = unitIntAt(args, 0) == 1;
        out.emplace_back(static_cast<int64_t>(first ? it.damageType : 0));
        out.emplace_back(first ? it.minDamage : 0.0);
        out.emplace_back(first ? it.maxDamage : 0.0);
    }
    else if (op == "item.isBag")
    {
        // the host answers 1 / 0 / -1 (no template); the binding pushes the bool
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.bag ? 1 : 0) : -1));
    }
    else if (op == "item.isSoulBound")
        out.emplace_back(it.soulBound);
    else if (op == "item.isBoundAccountWide")
    {
        // the host answers 1 / 0 / -1 (no template); the binding pushes the bool
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.accountWide ? 1 : 0) : -1));
    }
    else if (op == "item.isBoundByEnchant")
        out.emplace_back(it.boundByEnchant);
    else if (op == "item.isNotBoundToPlayer")
        out.emplace_back(it.boundToPlayerAnswer ? false : it.notBoundToPlayer);
    else if (op == "item.isLocked")
        out.emplace_back(it.locked);
    else if (op == "item.isBroken")
        out.emplace_back(it.broken);
    else if (op == "item.isCurrencyToken")
    {
        // the host answers 1 / 0 / -1 (no template); the binding pushes the bool
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.currencyToken ? 1 : 0) : -1));
    }
    else if (op == "item.isNotEmptyBag")
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.notEmptyBag ? 1 : 0) : -1));
    else if (op == "item.isInBag")
        out.emplace_back(it.inBag);
    else if (op == "item.isInTrade")
        out.emplace_back(it.inTrade);
    else if (op == "item.isPotion")
    {
        // the host answers 1 / 0 / -1 (no template); the binding pushes the bool
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.potion ? 1 : 0) : -1));
    }
    else if (op == "item.isConjuredConsumable")
    {
        // the host answers 1 / 0 / -1 (no template); the binding pushes the bool
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.conjured ? 1 : 0) : -1));
    }
    else if (op == "item.isRefundExpired")
        out.emplace_back(it.refundExpired);
    else if (op == "item.canBeTraded")
        out.emplace_back(static_cast<int64_t>(it.hasTemplate ? (it.tradable ? 1 : 0) : -1));
    else if (op == "item.hasQuest")
        out.emplace_back(static_cast<int64_t>(
            it.hasTemplate ? (it.quests.count(static_cast<uint32_t>(unitIntAt(args, 0))) ? 1 : 0) : -1));
    else if (op == "item.getOwnerGuid")
        out.emplace_back(it.ownerGuid);
    else if (op == "item.setBinding")
        it.binding = unitBoolAt(args, 0, false);
    else if (op == "item.getEnchantmentId")
    {
        uint32_t const slot = static_cast<uint32_t>(unitIntAt(args, 0));
        if (slot >= 13)
        {
            out.emplace_back(false);
            return true;
        }
        out.emplace_back(static_cast<int64_t>(it.enchantments[slot]));
    }
    else if (op == "item.setEnchantment")
    {
        uint32_t const enchantId = static_cast<uint32_t>(unitIntAt(args, 0));
        uint32_t const slot = static_cast<uint32_t>(unitIntAt(args, 1));
        if (slot >= 13 || !it.knownEnchants.count(enchantId) || it.ownerMissing)
        {
            out.emplace_back(false);   // the binding raises: not an enchant the server knows
            return true;
        }
        it.enchantments[slot] = enchantId;
        out.emplace_back(true);
    }
    else if (op == "item.clearEnchantment")
    {
        uint32_t const slot = static_cast<uint32_t>(unitIntAt(args, 0));
        if (slot >= 13 || !it.enchantments[slot] || it.ownerMissing)
        {
            out.emplace_back(false);
            return true;
        }
        it.enchantments[slot] = 0;
        out.emplace_back(true);
    }
    else if (op == "item.getExtraFlags")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.extraFlags));
    }
    else if (op == "item.getSpeed")
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.speed));
    }
    else  // item.getBagSize
    {
        if (!templated())
            return true;
        out.emplace_back(static_cast<int64_t>(it.bag ? it.bagSize : 0));
    }
    return true;
}

bool FakeHost::gameCall(fl::HandleData const& self, std::string_view op, std::vector<fl::EventValue> const& args,
                        std::vector<fl::EventValue>& out)
{
    noteMapLock();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        gameCalls.emplace_back(op);
    }
    if (onGameCall && onGameCall(self, std::string(op), args, out))
        return true;
    if (unitStateCall(self, std::string(op), args, out))
        return true;
    if (worldObjectStateCall(self, std::string(op), args, out))
        return true;
    if (self.kind == fl::ObjKind::Creature && creatureStateCall(self, std::string(op), args, out))
        return true;
    if (self.kind == fl::ObjKind::GameObject && gameObjectStateCall(self, std::string(op), args, out))
        return true;
    if (self.kind == fl::ObjKind::Quest && questStateCall(self, std::string(op), args, out))
        return true;
    if (self.kind == fl::ObjKind::Item && itemStateCall(self, std::string(op), args, out))
        return true;
    if (playerStateCall(self, std::string(op), args, out))
        return true;
    if (spellStateCall(self, std::string(op), args, out))
        return true;
    auto it = gameResults.find(std::string(op));
    if (it == gameResults.end())
        return false;
    out = it->second;
    return true;
}

namespace
{
int64_t intAt(std::vector<fl::EventValue> const& a, size_t i, int64_t def = 0)
{
    if (i >= a.size())
        return def;
    if (auto const* v = std::get_if<int64_t>(&a[i].v))
        return *v;
    if (auto const* d = std::get_if<double>(&a[i].v))
        return static_cast<int64_t>(*d);
    return def;
}
double doubleAt(std::vector<fl::EventValue> const& a, size_t i, double def = 0.0)
{
    if (i >= a.size())
        return def;
    if (auto const* d = std::get_if<double>(&a[i].v))
        return *d;
    if (auto const* v = std::get_if<int64_t>(&a[i].v))
        return static_cast<double>(*v);
    return def;
}
bool boolAt(std::vector<fl::EventValue> const& a, size_t i, bool def = false)
{
    if (i >= a.size())
        return def;
    if (auto const* b = std::get_if<bool>(&a[i].v))
        return *b;
    return def;
}
std::string stringAt(std::vector<fl::EventValue> const& a, size_t i)
{
    if (i >= a.size())
        return std::string();
    if (auto const* s = std::get_if<std::string>(&a[i].v))
        return *s;
    return std::string();
}
bool handleAt(std::vector<fl::EventValue> const& a, size_t i, fl::HandleData& out)
{
    if (i >= a.size())
        return false;
    if (auto const* h = std::get_if<fl::HandleData>(&a[i].v))
    {
        out = *h;
        return true;
    }
    return false;
}
}  // namespace

bool FakeHost::spellStateCall(fl::HandleData const& self, std::string const& op,
                              std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    using FakeSI = FakeSpellInfo;
    // The SpellInfo readers of the round, as member tables: the same shape as the real host's op tables, over the fields a
    // test sets. The core's own bounds (an effect list shorter than MAX_SPELL_EFFECTS) are modelled: out of range throws
    // std::out_of_range, which fl::safe turns into a Lua error, exactly as the real host does.
    struct IntField { char const* op; int64_t FakeSI::*member; };
    static IntField const kInts[] = {
        {"spellinfo.getCategory", &FakeSI::category},
        {"spellinfo.getDispel", &FakeSI::dispel},
        {"spellinfo.getMechanic", &FakeSI::mechanic},
        {"spellinfo.getSchoolMask", &FakeSI::schoolMask},
        {"spellinfo.getAttributes", &FakeSI::attributes},
        {"spellinfo.getAttributesCu", &FakeSI::attributesCu},
        {"spellinfo.getAttributesEx", &FakeSI::attributesEx},
        {"spellinfo.getAttributesEx2", &FakeSI::attributesEx2},
        {"spellinfo.getAttributesEx3", &FakeSI::attributesEx3},
        {"spellinfo.getAttributesEx4", &FakeSI::attributesEx4},
        {"spellinfo.getAttributesEx5", &FakeSI::attributesEx5},
        {"spellinfo.getAttributesEx6", &FakeSI::attributesEx6},
        {"spellinfo.getAttributesEx7", &FakeSI::attributesEx7},
        {"spellinfo.getDuration", &FakeSI::duration},
        {"spellinfo.getMaxDuration", &FakeSI::maxDuration},
        {"spellinfo.getRecoveryTime", &FakeSI::recoveryTime},
        {"spellinfo.getCategoryRecoveryTime", &FakeSI::categoryRecoveryTime},
        {"spellinfo.getStackAmount", &FakeSI::stackAmount},
        {"spellinfo.getMaxAffectedTargets", &FakeSI::maxAffectedTargets},
        {"spellinfo.getSpellFamilyName", &FakeSI::spellFamilyName},
        {"spellinfo.getExplicitTargetMask", &FakeSI::explicitTargetMask},
        {"spellinfo.getAuraState", &FakeSI::auraState},
        {"spellinfo.getSpellSpecific", &FakeSI::spellSpecific},
        {"spellinfo.getBaseLevel", &FakeSI::baseLevel},
        {"spellinfo.getSpellLevel", &FakeSI::spellLevel},
        {"spellinfo.getMaxLevel", &FakeSI::maxLevel},
        {"spellinfo.getMaxTargetLevel", &FakeSI::maxTargetLevel},
        {"spellinfo.getDmgClass", &FakeSI::dmgClass},
        {"spellinfo.getPowerType", &FakeSI::powerType},
        {"spellinfo.getManaCost", &FakeSI::manaCost},
        {"spellinfo.getManaCostPerlevel", &FakeSI::manaCostPerLevel},
        {"spellinfo.getManaPerSecond", &FakeSI::manaPerSecond},
        {"spellinfo.getProcChance", &FakeSI::procChance},
        {"spellinfo.getProcCharges", &FakeSI::procCharges},
        {"spellinfo.getProcFlags", &FakeSI::procFlags},
        {"spellinfo.getRank", &FakeSI::rank},
        {"spellinfo.getTargets", &FakeSI::targets},
        {"spellinfo.getTargetCreatureType", &FakeSI::targetCreatureType},
        {"spellinfo.getInterruptFlags", &FakeSI::interruptFlags},
        {"spellinfo.getAuraInterruptFlags", &FakeSI::auraInterruptFlags},
        {"spellinfo.getChannelInterruptFlags", &FakeSI::channelInterruptFlags},
        {"spellinfo.getStartRecoveryCategory", &FakeSI::startRecoveryCategory},
        {"spellinfo.getStartRecoveryTime", &FakeSI::startRecoveryTime},
        {"spellinfo.getPreventionType", &FakeSI::preventionType},
        {"spellinfo.getAttackType", &FakeSI::attackType},
        {"spellinfo.getActiveIconID", &FakeSI::activeIconID},
        {"spellinfo.getSpellIconID", &FakeSI::spellIconID},
        {"spellinfo.getRequiresSpellFocus", &FakeSI::requiresSpellFocus},
        {"spellinfo.getAreaGroupId", &FakeSI::areaGroupId},
        {"spellinfo.getCasterAuraSpell", &FakeSI::casterAuraSpell},
        {"spellinfo.getCasterAuraState", &FakeSI::casterAuraState},
        {"spellinfo.getCasterAuraStateNot", &FakeSI::casterAuraStateNot},
        {"spellinfo.getTargetAuraSpell", &FakeSI::targetAuraSpell},
        {"spellinfo.getTargetAuraState", &FakeSI::targetAuraState},
        {"spellinfo.getTargetAuraStateNot", &FakeSI::targetAuraStateNot},
        {"spellinfo.getFacingCasterFlags", &FakeSI::facingCasterFlags},
        {"spellinfo.getEquippedItemClass", &FakeSI::equippedItemClass},
        {"spellinfo.getEquippedItemSubClassMask", &FakeSI::equippedItemSubClassMask},
        {"spellinfo.getEquippedItemInventoryTypeMask", &FakeSI::equippedItemInventoryTypeMask},
        {"spellinfo.getStances", &FakeSI::stances},
        {"spellinfo.getStancesNot", &FakeSI::stancesNot},
        {"spellinfo.calcCastTime", &FakeSI::calcCastTime},
    };
    struct FloatField { char const* op; double FakeSI::*member; };
    static FloatField const kFloats[] = {
        {"spellinfo.getMaxRange", &FakeSI::maxRange},
        {"spellinfo.getMinRange", &FakeSI::minRange},
        {"spellinfo.getSpeed", &FakeSI::speed},
    };
    struct BoolField { char const* op; bool FakeSI::*member; };
    static BoolField const kBools[] = {
        {"spellinfo.hasAreaAuraEffect", &FakeSI::hasAreaAuraEffect},
        {"spellinfo.hasOnlyDamageEffects", &FakeSI::hasOnlyDamageEffects},
        {"spellinfo.hasInitialAggro", &FakeSI::hasInitialAggro},
        {"spellinfo.isPassive", &FakeSI::passive},
        {"spellinfo.isAutocastable", &FakeSI::autocastable},
        {"spellinfo.isStackableWithRanks", &FakeSI::stackableWithRanks},
        {"spellinfo.isChanneled", &FakeSI::channeled},
        {"spellinfo.isPositive", &FakeSI::positive},
        {"spellinfo.isAffectingArea", &FakeSI::affectingArea},
        {"spellinfo.isTargetingArea", &FakeSI::targetingArea},
        {"spellinfo.needsExplicitUnitTarget", &FakeSI::needsExplicitUnitTarget},
        {"spellinfo.isDeathPersistent", &FakeSI::deathPersistent},
        {"spellinfo.isRequiringDeadTarget", &FakeSI::requiringDeadTarget},
        {"spellinfo.isAllowingDeadTarget", &FakeSI::allowingDeadTarget},
        {"spellinfo.canBeUsedInCombat", &FakeSI::canBeUsedInCombat},
        {"spellinfo.isCooldownStartedOnEvent", &FakeSI::cooldownStartedOnEvent},
        {"spellinfo.isRanked", &FakeSI::ranked},
        {"spellinfo.isSingleTarget", &FakeSI::singleTarget},
        {"spellinfo.isMultiSlotAura", &FakeSI::multiSlotAura},
        {"spellinfo.isGroupBuff", &FakeSI::groupBuff},
        {"spellinfo.isMoveAllowedChannel", &FakeSI::moveAllowedChannel},
        {"spellinfo.isNextMeleeSwingSpell", &FakeSI::nextMeleeSwingSpell},
        {"spellinfo.isRangedWeaponSpell", &FakeSI::rangedWeaponSpell},
        {"spellinfo.isAutoRepeatRangedSpell", &FakeSI::autoRepeatRangedSpell},
        {"spellinfo.isAffectedBySpellMods", &FakeSI::affectedBySpellMods},
        {"spellinfo.isAbilityLearnedWithProfession", &FakeSI::abilityLearnedWithProfession},
        {"spellinfo.isBreakingStealth", &FakeSI::breakingStealth},
        {"spellinfo.isExplicitDiscovery", &FakeSI::explicitDiscovery},
        {"spellinfo.isLootCrafting", &FakeSI::lootCrafting},
        {"spellinfo.isPassiveStackableWithRanks", &FakeSI::passiveStackableWithRanks},
        {"spellinfo.isPrimaryProfession", &FakeSI::primaryProfession},
        {"spellinfo.isPrimaryProfessionFirstRank", &FakeSI::primaryProfessionFirstRank},
        {"spellinfo.isProfession", &FakeSI::profession},
        {"spellinfo.isProfessionOrRiding", &FakeSI::professionOrRiding},
        {"spellinfo.isStackableOnOneSlotWithDifferentCasters", &FakeSI::stackableOnOneSlotWithDifferentCasters},
        {"spellinfo.needsComboPoints", &FakeSI::needsComboPoints},
    };
    static IntField const kExtInts[] = {
        {"spellinfo.getAllEffectsMechanicMask", &FakeSI::allEffectsMechanicMask},
        {"spellinfo.getAllowedMechanicMask", &FakeSI::allowedMechanicMask},
        {"spellinfo.getExcludeCasterAuraSpell", &FakeSI::excludeCasterAuraSpell},
        {"spellinfo.getExcludeTargetAuraSpell", &FakeSI::excludeTargetAuraSpell},
        {"spellinfo.getManaCostPercentage", &FakeSI::manaCostPercentage},
        {"spellinfo.getMaxTicks", &FakeSI::maxTicks},
        {"spellinfo.getDiminishingReturnsGroupForSpell", &FakeSI::diminishingGroup},
        {"spellinfo.getDiminishingReturnsGroupType", &FakeSI::diminishingGroupType},
        {"spellinfo.getDiminishingReturnsLimitDuration", &FakeSI::diminishingLimitDuration},
        {"spellinfo.getDiminishingReturnsMaxLevel", &FakeSI::diminishingMaxLevel},
    };

    if (self.kind == fl::ObjKind::Spell)
    {
        auto it = spells.find(self.instanceId);
        if (it == spells.end())
            return false;   // "Spell no longer exists"
        FakeSpell& sp = it->second;
        auto action = [this, &sp, &op, &args]
        {
            sp.actions.emplace_back(op, args);
            spellActions.push_back(op);
        };
        if (op == "spell.getEntry")
            out.emplace_back(static_cast<int64_t>(sp.id));
        else if (op == "spell.getCaster")
        {
            if (sp.hasCaster)
                out.emplace_back(sp.caster);
        }
        else if (op == "spell.getOriginalCaster")
        {
            if (sp.hasOriginalCaster)
                out.emplace_back(sp.originalCaster);
        }
        else if (op == "spell.getSpellInfo")
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::SpellInfo;
            h.guid.lo = sp.id;
            out.emplace_back(h);
        }
        else if (op == "spell.getCastTime")
            out.emplace_back(static_cast<int64_t>(sp.castTime));
        else if (op == "spell.getDelayStart")
            out.emplace_back(static_cast<int64_t>(sp.delayStart));
        else if (op == "spell.getDelayMoment")
            out.emplace_back(static_cast<int64_t>(sp.delayMoment));
        else if (op == "spell.setDelayStart")
        {
            int64_t const time = unitIntAt(args, 0);
            if (time < 0)
                throw std::out_of_range("a delay start time is not negative");
            sp.delayStart = static_cast<uint64_t>(time);
            action();
        }
        else if (op == "spell.getPowerCost")
            out.emplace_back(sp.powerCost.empty() ? int64_t(0) : static_cast<int64_t>(sp.powerCost.front().second));
        else if (op == "spell.getState")
            out.emplace_back(static_cast<int64_t>(sp.state));
        else if (op == "spell.setState")
        {
            sp.state = static_cast<uint32_t>(unitIntAt(args, 0));
            action();
        }
        else if (op == "spell.getTarget")
        {
            if (sp.hasTarget)
                out.emplace_back(sp.target);
        }
        else if (op == "spell.getTargetDest")
        {
            out.emplace_back(sp.hasDest ? sp.destX : 0.0);
            out.emplace_back(sp.hasDest ? sp.destY : 0.0);
            out.emplace_back(sp.hasDest ? sp.destZ : 0.0);
        }
        else if (op == "spell.isAutoRepeat")
            out.emplace_back(sp.autoRepeat);
        else if (op == "spell.setAutoRepeat")
        {
            sp.autoRepeat = unitBoolAt(args, 0);
            action();
        }
        else if (op == "spell.cast")
        {
            // The real host casts only a spell in SPELL_STATE_PREPARING (the state the core's own cast sites run in).
            if (sp.state != 1)
                throw std::out_of_range("spell state " + std::to_string(sp.state) +
                                        " cannot be cast (the core casts a spell from PREPARING only)");
            action();
        }
        else if (op == "spell.finish" || op == "spell.cancel" || op == "spell.cleanupTargetList")
            action();
        else if (op == "spell.getDuration")
            out.emplace_back(static_cast<int64_t>(sp.duration));
        else if (op == "spell.isPositive")
            out.emplace_back(sp.positive);
        else if (op == "spell.isTriggered")
            out.emplace_back(sp.triggered);
        else if (op == "spell.isChannelActive")
            out.emplace_back(sp.channelActive);
        else if (op == "spell.isIgnoringCooldowns")
            out.emplace_back(sp.ignoringCooldowns);
        else if (op == "spell.isInterruptable")
            out.emplace_back(sp.interruptable);
        else if (op == "spell.isNeedSendToClient")
            out.emplace_back(sp.needSendToClient);
        else if (op == "spell.isProcDisabled")
            out.emplace_back(sp.procDisabled);
        else if (op == "spell.isDeletable")
            out.emplace_back(sp.deletable);
        else if (op == "spell.isAutoActionResetSpell")
            out.emplace_back(sp.autoActionReset);
        else if (op == "spell.isTriggeredByAura")
            out.emplace_back(sp.triggeredBySpellId == static_cast<uint32_t>(unitIntAt(args, 0)));
        else if (op == "spell.setExecutedCurrently")
        {
            sp.executedCurrently = unitBoolAt(args, 0);
            action();
        }
        else if (op == "spell.setReferencedFromCurrent")
        {
            sp.referencedFromCurrent = unitBoolAt(args, 0);
            action();
        }
        else if (op == "spell.getRuneState")
            out.emplace_back(static_cast<int64_t>(sp.runeState));
        else if (op == "spell.setRuneState")
        {
            sp.runeState = static_cast<uint32_t>(unitIntAt(args, 0));
            action();
        }
        else if (op == "spell.setSpellValue")
        {
            // SpellValueMod 0..39 (SPELLVALUE_DURATION_PCT is the last one), as the real host checks.
            int64_t const mod = unitIntAt(args, 0);
            if (mod < 0 || mod > 39)
                throw std::out_of_range("spell value mod " + std::to_string(mod) + " is not 0..39");
            action();
        }
        else if (op == "spell.updatePointers")
        {
            action();
            out.emplace_back(true);
        }
        else if (op == "spell.calculateDamage")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index >= sp.effectCount)
                throw std::out_of_range("effect index " + std::to_string(index) + " is not an effect of spell " +
                                        std::to_string(sp.id));
            out.emplace_back(static_cast<int64_t>(sp.damage[index]));
        }
        else if (op == "spell.getUnitTargetCountForEffect" || op == "spell.getGameObjectTargetCountForEffect" ||
                 op == "spell.getItemTargetCountForEffect")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index >= 32)
                throw std::out_of_range("effect index " + std::to_string(index) + " is not below MAX_SPELL_EFFECTS");
            if (op == "spell.getUnitTargetCountForEffect")
                out.emplace_back(sp.unitTargets[index]);
            else if (op == "spell.getGameObjectTargetCountForEffect")
                out.emplace_back(sp.goTargets[index]);
            else
                out.emplace_back(sp.itemTargets[index]);
        }
        else
            return false;
        return true;
    }
    if (self.kind == fl::ObjKind::Aura)
    {
        auto it = auras.find(self.instanceId);
        if (it == auras.end())
            return false;   // "Aura no longer exists"
        FakeAura& aura = it->second;
        if (op == "aura.getAuraId" || op == "aura.getEntry")
            out.emplace_back(static_cast<int64_t>(aura.id));
        else if (op == "aura.getCaster")
        {
            if (aura.hasCaster)
                out.emplace_back(aura.caster);
        }
        else if (op == "aura.getCasterGuid")
            out.emplace_back(guidToString(aura.casterGuid));
        else if (op == "aura.getCasterLevel")
            out.emplace_back(static_cast<int64_t>(aura.casterLevel));
        else if (op == "aura.getDuration")
            out.emplace_back(static_cast<int64_t>(aura.duration));
        else if (op == "aura.setDuration")
        {
            aura.duration = static_cast<int32_t>(unitIntAt(args, 0));
            aura.actions.push_back("setDuration");
            auraActions.push_back("setDuration");
        }
        else if (op == "aura.getMaxDuration")
            out.emplace_back(static_cast<int64_t>(aura.maxDuration));
        else if (op == "aura.setMaxDuration")
        {
            aura.maxDuration = static_cast<int32_t>(unitIntAt(args, 0));
            aura.actions.push_back("setMaxDuration");
            auraActions.push_back("setMaxDuration");
        }
        else if (op == "aura.getStackAmount")
            out.emplace_back(static_cast<int64_t>(aura.stackAmount));
        else if (op == "aura.setStackAmount")
        {
            // The real host's rules (SpellAuras.cpp:1061 direct setter plus the limits ModStackAmount applies at
            // SpellAuras.cpp:1097-1112): zero removes the aura, an increase is clamped to CalcMaxStackAmount (at least
            // one stack for a non-stackable aura).
            uint32_t const amount = static_cast<uint32_t>(unitIntAt(args, 0));
            if (amount == 0)
            {
                aura.actions.push_back("remove");
                auraActions.push_back("remove");
                return true;
            }
            uint32_t const maxAmount = std::max(aura.maxStackAmount, 1u);
            uint32_t const clamped = std::min(amount, maxAmount);
            uint32_t const old = aura.stackAmount;
            aura.stackAmount = clamped;
            aura.actions.push_back("setStackAmount");
            auraActions.push_back("setStackAmount");
            if (clamped >= old)
            {
                aura.actions.push_back("refresh");   // docs: the duration resets to the maximum
                auraActions.push_back("refresh");
            }
        }
        else if (op == "aura.getOwner")
        {
            if (aura.hasOwner)
                out.emplace_back(aura.owner);
        }
        else if (op == "aura.getSpellInfo")
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::SpellInfo;
            h.guid.lo = aura.id;
            out.emplace_back(h);
        }
        else if (op == "aura.remove")
        {
            aura.actions.push_back("remove");
            auraActions.push_back("remove");
        }
        else
            return false;
        return true;
    }
    if (self.kind == fl::ObjKind::SpellInfo)
    {
        auto it = spellInfos.find(static_cast<uint32_t>(self.guid.lo));
        if (it == spellInfos.end())
            return false;
        FakeSI const& info = it->second;
        if (op == "spellinfo.getId")
        {
            out.emplace_back(static_cast<int64_t>(info.id));
            return true;
        }
        for (IntField const& f : kInts)
            if (op == f.op)
            {
                out.emplace_back(info.*(f.member));
                return true;
            }
        for (FloatField const& f : kFloats)
            if (op == f.op)
            {
                out.emplace_back(info.*(f.member));
                return true;
            }
        for (BoolField const& f : kBools)
            if (op == f.op)
            {
                out.emplace_back(info.*(f.member));
                return true;
            }
        if (op == "spellinfo.isSelfCast")
        {
            bool hasEffect = false;
            bool selfCast = true;
            for (FakeSI::FakeEffect const& e : info.effectSlots)
            {
                if (!e.isEffect)
                    continue;
                hasEffect = true;
                if (e.targetA.target != 1 || (e.targetB.target != 0 && e.targetB.target != 1))
                {
                    selfCast = false;
                    break;
                }
            }
            out.emplace_back(hasEffect && selfCast);
            return true;
        }
        for (IntField const& f : kExtInts)
            if (op == f.op)
            {
                out.emplace_back(info.*(f.member));
                return true;
            }
        if (op == "spellinfo.getSpellVisual")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index > 1)
                throw std::out_of_range("spell visual index " + std::to_string(index) + " is not 0..1");
            out.emplace_back(info.spellVisual[index]);
            return true;
        }
        if (op == "spellinfo.getSpellMechanicMaskByEffectMask")
        {
            uint32_t const mask = static_cast<uint32_t>(unitIntAt(args, 0));
            int64_t outMask = info.mechanic ? (int64_t(1) << info.mechanic) : 0;
            for (size_t i = 0; i < info.effectSlots.size() && i < 32; ++i)
                if ((mask & (1u << i)) && info.effectSlots[i].mechanic)
                    outMask |= (int64_t(1) << info.effectSlots[i].mechanic);
            out.emplace_back(outMask);
            return true;
        }
        if (op == "spellinfo.checkTarget")
        {
            out.emplace_back(info.checkTargetResult);
            return true;
        }
        if (op == "spellinfo.checkTargetCreatureType")
        {
            fl::HandleData h;
            handleAt(args, 0, h);
            out.emplace_back(info.checkTargetCreatureTypes.count(h.guid.lo) != 0);
            return true;
        }
        if (op == "spellinfo.isAbilityOfSkillType")
        {
            out.emplace_back(info.skillTypes.count(static_cast<uint32_t>(unitIntAt(args, 0))) != 0);
            return true;
        }
        if (op == "spellinfo.isAffected")
        {
            uint32_t const fam = static_cast<uint32_t>(unitIntAt(args, 0));
            uint32_t const f0 = static_cast<uint32_t>(unitIntAt(args, 1));
            uint32_t const f1 = static_cast<uint32_t>(unitIntAt(args, 2));
            uint32_t const f2 = static_cast<uint32_t>(unitIntAt(args, 3));
            bool ok = (fam == 0 || fam == info.affectedFamilyName) &&
                      ((f0 == 0 && f1 == 0 && f2 == 0) || (f0 & info.affectedFlags[0]) ||
                       (f1 & info.affectedFlags[1]) || (f2 & info.affectedFlags[2]));
            out.emplace_back(ok);
            return true;
        }
        if (op == "spellinfo.isItemFitToSpellRequirements")
        {
            fl::HandleData h;
            handleAt(args, 0, h);
            out.emplace_back(info.fitItems.count(h.guid.lo) != 0);
            return true;
        }
        if (op == "spellinfo.spellCancelsAuraEffect")
        {
            uint32_t const aeId = static_cast<uint32_t>(unitIntAt(args, 0));
            if (auraEffects.count(aeId) == 0)
                throw std::out_of_range("AuraEffect no longer exists");
            out.emplace_back(info.cancelsAuraEffects.count(aeId) != 0);
            return true;
        }
        auto effSlot = [&](uint32_t idx) -> FakeSI::FakeEffect const&
        {
            if (idx >= info.effectSlots.size())
                throw std::out_of_range("effect index " + std::to_string(idx) + " is not an effect of spell " +
                                        std::to_string(info.id));
            return info.effectSlots[idx];
        };
        if (op.rfind("spellinfo.effectIs", 0) == 0 || op.rfind("spellinfo.getEffect", 0) == 0)
        {
            FakeSI::FakeEffect const& e = effSlot(static_cast<uint32_t>(unitIntAt(args, 0)));
            if (op == "spellinfo.effectIsAreaAuraEffect") out.emplace_back(e.isAreaAuraEffect);
            else if (op == "spellinfo.effectIsAura") out.emplace_back(e.isAura);
            else if (op == "spellinfo.effectIsAuraType")
                out.emplace_back(e.isAura && e.auraType == unitIntAt(args, 1));
            else if (op == "spellinfo.effectIsEffect") out.emplace_back(e.isEffect);
            else if (op == "spellinfo.effectIsEffectType")
                out.emplace_back(e.isEffect && e.effectType == unitIntAt(args, 1));
            else if (op == "spellinfo.effectIsTargetingArea") out.emplace_back(e.isTargetingArea);
            else if (op == "spellinfo.effectIsUnitOwnedAuraEffect") out.emplace_back(e.isUnitOwnedAuraEffect);
            else if (op == "spellinfo.getEffectAmplitude") out.emplace_back(e.amplitude);
            else if (op == "spellinfo.getEffectApplyAuraName") out.emplace_back(e.auraType);
            else if (op == "spellinfo.getEffectBasePoints") out.emplace_back(e.basePoints);
            else if (op == "spellinfo.getEffectBonusMultiplier") out.emplace_back(e.bonusMultiplier);
            else if (op == "spellinfo.getEffectCalcRadius") out.emplace_back(e.calcRadius);
            else if (op == "spellinfo.getEffectCalcValue") out.emplace_back(e.calcValue);
            else if (op == "spellinfo.getEffectChainTarget") out.emplace_back(e.chainTarget);
            else if (op == "spellinfo.getEffectDamageMultiplier") out.emplace_back(e.damageMultiplier);
            else if (op == "spellinfo.getEffectDieSides") out.emplace_back(e.dieSides);
            else if (op == "spellinfo.getEffectHasRadius") out.emplace_back(e.hasRadius);
            else if (op == "spellinfo.getEffectImplicitTargetType") out.emplace_back(e.implicitTargetType);
            else if (op == "spellinfo.getEffectIndex") out.emplace_back(e.effectIndex);
            else if (op == "spellinfo.getEffectItemType") out.emplace_back(e.itemType);
            else if (op == "spellinfo.getEffectMechanic") out.emplace_back(e.mechanic);
            else if (op == "spellinfo.getEffectMechanicMask") out.emplace_back(e.mechanicMask);
            else if (op == "spellinfo.getEffectMiscValue") out.emplace_back(e.miscValue);
            else if (op == "spellinfo.getEffectMiscValueB") out.emplace_back(e.miscValueB);
            else if (op == "spellinfo.getEffectPointsPerComboPoint") out.emplace_back(e.pointsPerComboPoint);
            else if (op == "spellinfo.getEffectProvidedTargetMask") out.emplace_back(e.providedTargetMask);
            else if (op == "spellinfo.getEffectRealPointsPerLevel") out.emplace_back(e.realPointsPerLevel);
            else if (op == "spellinfo.getEffectSpellClassMask")
            {
                uint32_t const mi = static_cast<uint32_t>(unitIntAt(args, 1));
                if (mi > 2)
                    throw std::out_of_range("spell class mask index " + std::to_string(mi) + " is not 0..2");
                out.emplace_back(e.spellClassMask[mi]);
            }
            else if (op == "spellinfo.getEffectTargetACheckType") out.emplace_back(e.targetA.checkType);
            else if (op == "spellinfo.getEffectTargetADirectionAngle") out.emplace_back(e.targetA.directionAngle);
            else if (op == "spellinfo.getEffectTargetADirectionType") out.emplace_back(e.targetA.directionType);
            else if (op == "spellinfo.getEffectTargetAIsArea") out.emplace_back(e.targetA.isArea);
            else if (op == "spellinfo.getEffectTargetAObjectType") out.emplace_back(e.targetA.objectType);
            else if (op == "spellinfo.getEffectTargetAReferenceType") out.emplace_back(e.targetA.referenceType);
            else if (op == "spellinfo.getEffectTargetASelectionCategory") out.emplace_back(e.targetA.selectionCategory);
            else if (op == "spellinfo.getEffectTargetATarget") out.emplace_back(e.targetA.target);
            else if (op == "spellinfo.getEffectTargetBCheckType") out.emplace_back(e.targetB.checkType);
            else if (op == "spellinfo.getEffectTargetBDirectionAngle") out.emplace_back(e.targetB.directionAngle);
            else if (op == "spellinfo.getEffectTargetBDirectionType") out.emplace_back(e.targetB.directionType);
            else if (op == "spellinfo.getEffectTargetBIsArea") out.emplace_back(e.targetB.isArea);
            else if (op == "spellinfo.getEffectTargetBObjectType") out.emplace_back(e.targetB.objectType);
            else if (op == "spellinfo.getEffectTargetBReferenceType") out.emplace_back(e.targetB.referenceType);
            else if (op == "spellinfo.getEffectTargetBSelectionCategory") out.emplace_back(e.targetB.selectionCategory);
            else if (op == "spellinfo.getEffectTargetBTarget") out.emplace_back(e.targetB.target);
            else if (op == "spellinfo.getEffectTriggerSpell") out.emplace_back(e.triggerSpell);
            else if (op == "spellinfo.getEffectType") out.emplace_back(e.effectType);
            else if (op == "spellinfo.getEffectUsedTargetObjectType") out.emplace_back(e.usedTargetObjectType);
            else if (op == "spellinfo.getEffectValueMultiplier") out.emplace_back(e.valueMultiplier);
            else
                return false;
            return true;
        }
        if (op == "spellinfo.getSpellFamilyFlags")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index > 2)
                throw std::out_of_range("spell family flags index " + std::to_string(index) + " is not 0..2");
            out.emplace_back(info.spellFamilyFlags[index]);
            return true;
        }
        if (op == "spellinfo.getTotem" || op == "spellinfo.getTotemCategory")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index >= 2)
                throw std::out_of_range("totem index " + std::to_string(index) + " is not 0..1");
            out.emplace_back(op == "spellinfo.getTotem" ? info.totem[index] : info.totemCategory[index]);
            return true;
        }
        if (op == "spellinfo.getReagent" || op == "spellinfo.getReagentCount")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index >= 8)
                throw std::out_of_range("reagent index " + std::to_string(index) + " is not 0..7");
            out.emplace_back(op == "spellinfo.getReagent" ? info.reagent[index] : info.reagentCount[index]);
            return true;
        }
        if (op == "spellinfo.hasEffect")
        {
            out.emplace_back(info.effects.count(unitIntAt(args, 0)) != 0);
            return true;
        }
        if (op == "spellinfo.hasAura")
        {
            out.emplace_back(info.auraTypes.count(unitIntAt(args, 0)) != 0);
            return true;
        }
        if (op == "spellinfo.isPositiveEffect")
        {
            uint32_t const index = static_cast<uint32_t>(unitIntAt(args, 0));
            if (index >= 32)
                throw std::out_of_range("effect index " + std::to_string(index) + " is not below MAX_SPELL_EFFECTS");
            out.emplace_back(info.negativeEffects.count(index) == 0);
            return true;
        }
        std::set<uint32_t> const* related = nullptr;
        if (op == "spellinfo.needsToBeTriggeredByCaster")
            related = &info.triggeredBy;
        else if (op == "spellinfo.isRankOf")
            related = &info.sameRankOf;
        else if (op == "spellinfo.isDifferentRankOf")
            related = &info.differentRankOf;
        else if (op == "spellinfo.isHighRankOf")
            related = &info.highRankOf;
        else if (op == "spellinfo.canDispelAura")
            related = &info.dispels;
        else if (op == "spellinfo.canPierceImmuneAura")
            related = &info.pierces;
        else if (op == "spellinfo.canSpellProvideImmunityAgainstAura")
            related = &info.providesImmunity;
        else if (op == "spellinfo.isAuraExclusiveBySpecificWith")
            related = &info.exclusiveWith;
        else if (op == "spellinfo.isAuraExclusiveBySpecificPerCasterWith")
            related = &info.exclusivePerCasterWith;
        if (related)
        {
            out.emplace_back(related->count(static_cast<uint32_t>(unitIntAt(args, 0))) != 0);
            return true;
        }
        return false;
    }
    if (self.kind == fl::ObjKind::AuraEffect)
    {
        auto it = auraEffects.find(self.instanceId);
        if (it == auraEffects.end())
            return false;
        FakeAuraEffect& ae = it->second;
        if (op == "auraeffect.calculateAmount")
            out.emplace_back(static_cast<int64_t>(ae.calculatedAmount));
        else if (op == "auraeffect.calculatePeriodic")
        {
            ae.actions.push_back("calculatePeriodic");
            auraEffectActions.push_back("calculatePeriodic");
        }
        else if (op == "auraeffect.canBeRecalculated")
            out.emplace_back(ae.canBeRecalculated);
        else if (op == "auraeffect.changeAmount")
        {
            ae.amount = static_cast<int32_t>(unitIntAt(args, 0));
            ae.actions.push_back("changeAmount");
            auraEffectActions.push_back("changeAmount");
        }
        else if (op == "auraeffect.getAmount")
            out.emplace_back(static_cast<int64_t>(ae.amount));
        else if (op == "auraeffect.getAmplitude")
            out.emplace_back(static_cast<int64_t>(ae.amplitude));
        else if (op == "auraeffect.getAuraType")
            out.emplace_back(ae.auraType);
        else if (op == "auraeffect.getBase")
        {
            if (ae.hasBase)
            {
                fl::HandleData h;
                h.kind = fl::ObjKind::Aura;
                h.instanceId = ae.baseAuraId;
                h.mapId = ae.id;
                out.emplace_back(h);
            }
        }
        else if (op == "auraeffect.getBaseAmount")
            out.emplace_back(static_cast<int64_t>(ae.baseAmount));
        else if (op == "auraeffect.getCaster")
        {
            if (ae.hasCaster)
                out.emplace_back(ae.caster);
        }
        else if (op == "auraeffect.getCasterGuid")
            out.emplace_back(guidToString(ae.casterGuid));
        else if (op == "auraeffect.getEffIndex")
            out.emplace_back(static_cast<int64_t>(ae.effIndex));
        else if (op == "auraeffect.getId")
            out.emplace_back(static_cast<int64_t>(ae.id));
        else if (op == "auraeffect.getPeriodicTimer")
            out.emplace_back(static_cast<int64_t>(ae.periodicTimer));
        else if (op == "auraeffect.getRemainingTicks")
            out.emplace_back(static_cast<int64_t>(ae.remainingTicks));
        else if (op == "auraeffect.getSpellInfo")
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::SpellInfo;
            h.guid.lo = ae.id;
            out.emplace_back(h);
        }
        else if (op == "auraeffect.getTargetList")
        {
            for (fl::HandleData const& th : ae.targetList)
                out.emplace_back(th);
        }
        else if (op == "auraeffect.getTickNumber")
            out.emplace_back(static_cast<int64_t>(ae.tickNumber));
        else if (op == "auraeffect.getTotalTicks")
            out.emplace_back(static_cast<int64_t>(ae.totalTicks));
        else if (op == "auraeffect.recalculateAmount")
        {
            if (ae.canBeRecalculated)
                ae.amount = ae.calculatedAmount;
            ae.actions.push_back("recalculateAmount");
            auraEffectActions.push_back("recalculateAmount");
        }
        else if (op == "auraeffect.setAmount")
        {
            ae.amount = static_cast<int32_t>(unitIntAt(args, 0));
            ae.canBeRecalculated = false;
        }
        else if (op == "auraeffect.setCanBeRecalculated")
            ae.canBeRecalculated = unitBoolAt(args, 0);
        else if (op == "auraeffect.setPeriodicTimer")
            ae.periodicTimer = static_cast<int32_t>(unitIntAt(args, 0));
        else
            return false;
        return true;
    }
    if (self.kind == fl::ObjKind::ElunaProcInfo)
    {
        auto it = procInfos.find(self.instanceId);
        if (it == procInfos.end())
            return false;
        FakeProcInfo& pi = it->second;
        if (op == "procinfo.getActionTarget")
        {
            if (pi.hasActionTarget)
                out.emplace_back(pi.actionTarget);
        }
        else if (op == "procinfo.getActor")
        {
            if (pi.hasActor)
                out.emplace_back(pi.actor);
        }
        else if (op == "procinfo.getAttackType")
            out.emplace_back(static_cast<int64_t>(pi.attackType));
        else if (op == "procinfo.getBlock")
            out.emplace_back(static_cast<int64_t>(pi.block));
        else if (op == "procinfo.getDamage")
            out.emplace_back(static_cast<int64_t>(pi.damage));
        else if (op == "procinfo.getDamageAbsorb")
            out.emplace_back(static_cast<int64_t>(pi.damageAbsorb));
        else if (op == "procinfo.getDamageType")
            out.emplace_back(static_cast<int64_t>(pi.damageType));
        else if (op == "procinfo.getEffectiveHeal")
            out.emplace_back(static_cast<int64_t>(pi.effectiveHeal));
        else if (op == "procinfo.getHeal")
            out.emplace_back(static_cast<int64_t>(pi.heal));
        else if (op == "procinfo.getHealAbsorb")
            out.emplace_back(static_cast<int64_t>(pi.healAbsorb));
        else if (op == "procinfo.getHitMask")
            out.emplace_back(static_cast<int64_t>(pi.hitMask));
        else if (op == "procinfo.getMap")
        {
            if (pi.hasMap)
                out.emplace_back(pi.map);
        }
        else if (op == "procinfo.getProcSpell")
        {
            if (pi.hasProcSpell)
                out.emplace_back(pi.procSpell);
        }
        else if (op == "procinfo.getResist")
            out.emplace_back(static_cast<int64_t>(pi.resist));
        else if (op == "procinfo.getSchoolMask")
            out.emplace_back(static_cast<int64_t>(pi.schoolMask));
        else if (op == "procinfo.getSpellInfo")
        {
            if (pi.spellId)
            {
                fl::HandleData h;
                h.kind = fl::ObjKind::SpellInfo;
                h.guid.lo = pi.spellId;
                out.emplace_back(h);
            }
        }
        else if (op == "procinfo.getSpellPhaseMask")
            out.emplace_back(static_cast<int64_t>(pi.spellPhaseMask));
        else if (op == "procinfo.getSpellTypeMask")
            out.emplace_back(static_cast<int64_t>(pi.spellTypeMask));
        else if (op == "procinfo.getTypeMask")
            out.emplace_back(static_cast<int64_t>(pi.typeMask));
        else if (op == "procinfo.hasDamage")
            out.emplace_back(pi.hasDamage);
        else if (op == "procinfo.hasHeal")
            out.emplace_back(pi.hasHeal);
        else if (op == "procinfo.setAbsorbDamage")
        {
            if (pi.hasDamage)
            {
                uint32_t const amt = std::min(static_cast<uint32_t>(unitIntAt(args, 0)), pi.damage);
                pi.damageAbsorb += amt;
                pi.damage -= amt;
            }
        }
        else if (op == "procinfo.setAbsorbHeal")
        {
            if (pi.hasHeal)
            {
                uint32_t const amt = std::min(static_cast<uint32_t>(unitIntAt(args, 0)), pi.heal);
                pi.healAbsorb += amt;
                pi.heal -= amt;
                pi.effectiveHeal -= std::min(amt, pi.effectiveHeal);
            }
        }
        else if (op == "procinfo.setBlockDamage")
        {
            if (pi.hasDamage)
            {
                uint32_t const amt = std::min(static_cast<uint32_t>(unitIntAt(args, 0)), pi.damage);
                pi.block += amt;
                pi.damage -= amt;
            }
        }
        else if (op == "procinfo.setDamage")
        {
            if (pi.hasDamage)
            {
                int64_t const target = unitIntAt(args, 0);
                pi.damage = target > 0 ? static_cast<uint32_t>(target) : 0u;
            }
        }
        else if (op == "procinfo.setEffectiveHeal")
        {
            if (pi.hasHeal)
                pi.effectiveHeal = static_cast<uint32_t>(unitIntAt(args, 0));
        }
        else if (op == "procinfo.setResistDamage")
        {
            if (pi.hasDamage)
            {
                uint32_t const amt = std::min(static_cast<uint32_t>(unitIntAt(args, 0)), pi.damage);
                pi.resist += amt;
                pi.damage -= amt;
            }
        }
        else
            return false;
        return true;
    }
    return false;
}

bool FakeHost::playerStateCall(fl::HandleData const& self, std::string const& op, std::vector<fl::EventValue> const& args,
                               std::vector<fl::EventValue>& out)
{
    // The documented Player methods of the long-tail round 1 (and Unit:SetPvP). Anything else is still gameResults.
    static std::set<std::string> const ops = {
        "player.getCoinage",          "player.setCoinage",           "player.modifyMoney",
        "player.getXP",               "player.getXPForNextLevel",    "player.getRestBonus",
        "player.setRestBonus",        "player.isRested",             "player.getHonorPoints",
        "player.setHonorPoints",      "player.modifyHonorPoints",    "player.getReputation",
        "player.setReputation",       "player.getReputationRank",    "player.getSkillValue",
        "player.getMaxSkillValue",    "player.getPureSkillValue",    "player.getPureMaxSkillValue",
        "player.getBaseSkillValue",   "player.getSkillPermBonusValue", "player.getSkillTempBonusValue",
        "player.hasSkill",            "player.setSkill",             "player.resetSpellCooldown",
        "player.hasSpellCooldown",    "player.getSpellCooldownDelay", "player.resetAllCooldowns",
        "player.resetTypeCooldowns",  "player.getItemByPos",         "player.getItemByEntry",
        "player.getItemByGUID",       "player.getEquippedItemBySlot", "player.equipItem",
        "player.canEquipItem",        "player.canUseItem",           "player.getQuestRewardStatus",
        "player.incompleteQuest",     "player.failQuest",            "player.removeQuest",
        "player.removeActiveQuest",   "player.removeRewardedQuest",  "player.canCompleteQuest",
        "player.canRewardQuest",      "player.rewardQuest",          "player.areaExploredOrEventHappens",
        "player.killedMonsterCredit", "player.groupEventHappens",    "player.talkedToCreature",
        "player.hasQuestForGO",       "player.hasQuestForItem",      "player.getReqKillOrCastCurrentCount",
        "player.hasReceivedQuestReward", "player.isInGroup",         "player.isInGuild",
        "player.isFlying",            "player.isMoving",             "player.isFalling",
        "player.isInWater",           "player.isTaxiCheater",        "player.setTaxiCheat",
        "player.inBattleground",      "player.getBattlegroundId",    "player.getBattlegroundTypeId",
        "player.isAFK",               "player.isDND",                "player.toggleAFK",
        "player.toggleDND",           "player.setFFA",               "player.isImmuneToDamage",
        "player.isImmuneToEnvironmentalDamage", "player.say",        "player.yell",
        "player.whisper",             "player.textEmote",            "player.sendShowBank",
        "player.sendListInventory",   "player.sendTrainerList",      "player.sendTaxiMenu",
        "player.setBindPoint",        "player.summonPlayer",         "player.getGuildName",
        "player.getDbLocaleIndex",    "player.getPlayerIP",          "player.getLatency",
        "player.getTotalPlayedTime",  "player.getLevelPlayedTime",   "player.logoutPlayer",
        "player.durabilityRepairAll", "unit.setPvP"};
    // E7 long tail, round 2: the documented reads.
    static std::set<std::string> const readOps = {
        "player.canBlock",              "player.canCompleteRepeatableQuest", "player.canFly",
        "player.canParry",              "player.canShareQuest",             "player.canSpeak",
        "player.canUninviteFromGroup",  "player.getChampioningFaction",     "player.getChatTag",
        "player.getDbcLocale",          "player.getDifficulty",             "player.getDrunkValue",
        "player.getGroupInvite",        "player.getGuildRank",              "player.getHealthBonusFromStamina",
        "player.getInGameTime",         "player.getLifetimeKills",          "player.getMailCount",
        "player.getMailItem",           "player.getManaBonusFromIntellect", "player.getNearbyGameObject",
        "player.getNextRandomRaidMember", "player.getOriginalGroup",         "player.getOriginalSubGroup",
        "player.getQuestLevel",         "player.getRecruiterId",            "player.getSelectedPlayer",
        "player.getSelectedUnit",       "player.getShieldBlockValue",       "player.getSubGroup",
        "player.getXPRestBonus",        "player.hasAtLoginFlag",            "player.hasPendingBind",
        "player.hasTalent",             "player.hasTitle",                  "player.inArena",
        "player.inBattlegroundQueue",   "player.isAcceptingWhispers",       "player.isARecruiter",
        "player.isGMChat",              "player.isGMVisible",               "player.isHonorOrXPTarget",
        "player.isGroupVisibleFor",     "player.isInSameGroupWith",         "player.isInSameRaidWith",
        "player.isNeverVisible",        "player.isOutdoorPvPActive",        "player.isVisibleForPlayer",
        "player.resetTalentsCost"};
    // E7 long tail, round 2: the writes.
    static std::set<std::string> const writeOps = {
        "player.sendCinematicStart",          "player.sendMovieStart",           "player.sendUpdateWorldState",
        "player.sendSpiritResurrect",         "player.setAcceptWhispers",        "player.setAtLoginFlag",
        "player.setDrunkValue",               "player.setFactionForRace",        "player.setGameMaster",
        "player.setGender",                   "player.setGMChat",                "player.setGMVisible",
        "player.setPvPDeath",                 "player.spawnBones",               "player.killPlayer",
        "player.resetTalents",                "player.durabilityLoss",           "player.durabilityLossAll",
        "player.durabilityPointLossForEquipSlot", "player.durabilityPointsLoss", "player.durabilityPointsLossAll",
        "player.durabilityRepair"};
    // E7 long tail, round 3: remaining Player methods.
    static std::set<std::string> const round3Ops = {
        "player.canTitanGrip",     "player.getComboPoints",           "player.getComboTarget",
        "player.getGossipTextId",  "player.hasRecruited",             "player.isRecruited",
        "player.addComboPoints",   "player.clearComboPoints",         "player.addTalent",
        "player.advanceAllSkills", "player.advanceSkill",             "player.advanceSkillsToMax", "player.bindToInstance",         "player.unbindInstance",
        "player.unbindAllInstances", "player.gossipSendPOI",          "player.killedPlayerCredit",
        "player.killGOCredit",     "player.leaveBattleground",        "player.mute",
        "player.removedInsignia",  "player.removeFromBattlegroundRaid", "player.removeFromGroup",
        "player.removePet",        "player.summonPet",                "player.sendAddonMessage",
        "player.sendAuctionMenu",  "player.sendQuestTemplate",        "player.sendShowMailBox",
        "player.sendTabardVendorActivate", "player.setKnownTitle",    "player.unsetKnownTitle",
        "player.setMovement",      "player.setPlayerLock",            "player.setQuestStatus",
        "player.startTaxi",        "player.groupCreate",              "player.groupInvite",
        "player.sendGuildInvite",  "player.setGuildRank",             "player.runCommand"};
    if (!ops.count(op) && !readOps.count(op) && !writeOps.count(op) && !round3Ops.count(op))
        return false;

    FakePlayer& p = playerState(self);
    auto pushItem = [this, &out](FakeItem const& it)
    {
        fl::HandleData h = item(it.low);
        fl::ObjectInfo info;
        info.entry = it.entry;
        add(h, info);
        out.emplace_back(h);
    };
    auto findItemByLow = [&p](uint64_t low) -> FakeItem*
    {
        for (FakeItem& it : p.items)
            if (it.low == low)
                return &it;
        return nullptr;
    };
    auto findItemByEntry = [&p](uint32_t entry) -> FakeItem*
    {
        for (FakeItem& it : p.items)
            if (it.entry == entry)
                return &it;
        return nullptr;
    };

    // ---- money, XP, rest ----
    if (op == "player.getCoinage")
        out.emplace_back(p.money);
    else if (op == "player.setCoinage")
        p.money = intAt(args, 0);
    else if (op == "player.modifyMoney")
        p.money += intAt(args, 0);
    else if (op == "player.getXP")
        out.emplace_back(static_cast<int64_t>(p.xp));
    else if (op == "player.getXPForNextLevel")
        out.emplace_back(static_cast<int64_t>(p.xpNext));
    else if (op == "player.getRestBonus")
        out.emplace_back(p.restBonus);
    else if (op == "player.setRestBonus")
        p.restBonus = doubleAt(args, 0);
    else if (op == "player.isRested")
        out.emplace_back(p.rested);
    // ---- honor and reputation ----
    else if (op == "player.getHonorPoints")
        out.emplace_back(static_cast<int64_t>(p.honor));
    else if (op == "player.setHonorPoints")
        p.honor = static_cast<uint32_t>(intAt(args, 0));
    else if (op == "player.modifyHonorPoints")
        p.honor = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(p.honor) + intAt(args, 0)));
    else if (op == "player.getReputation")
        out.emplace_back(static_cast<int64_t>(p.reputation[static_cast<uint32_t>(intAt(args, 0))]));
    else if (op == "player.setReputation")
        p.reputation[static_cast<uint32_t>(intAt(args, 0))] = static_cast<int32_t>(intAt(args, 1));
    else if (op == "player.getReputationRank")
        out.emplace_back(static_cast<int64_t>(p.repRank));
    // ---- skills ----
    else if (op == "player.getSkillValue")
        out.emplace_back(static_cast<int64_t>(p.skills[static_cast<uint32_t>(intAt(args, 0))].cur));
    else if (op == "player.getMaxSkillValue")
        out.emplace_back(static_cast<int64_t>(p.skills[static_cast<uint32_t>(intAt(args, 0))].max));
    else if (op == "player.getPureSkillValue")
    {
        FakeSkill const& s = p.skills[static_cast<uint32_t>(intAt(args, 0))];
        out.emplace_back(static_cast<int64_t>(s.cur) - s.perm - s.temp);
    }
    else if (op == "player.getPureMaxSkillValue")
    {
        FakeSkill const& s = p.skills[static_cast<uint32_t>(intAt(args, 0))];
        out.emplace_back(static_cast<int64_t>(s.max) - s.perm - s.temp);
    }
    else if (op == "player.getBaseSkillValue")
        out.emplace_back(static_cast<int64_t>(p.skills[static_cast<uint32_t>(intAt(args, 0))].cur) -
                         p.skills[static_cast<uint32_t>(intAt(args, 0))].temp);
    else if (op == "player.getSkillPermBonusValue")
        out.emplace_back(static_cast<int64_t>(p.skills[static_cast<uint32_t>(intAt(args, 0))].perm));
    else if (op == "player.getSkillTempBonusValue")
        out.emplace_back(static_cast<int64_t>(p.skills[static_cast<uint32_t>(intAt(args, 0))].temp));
    else if (op == "player.hasSkill")
        out.emplace_back(p.skills[static_cast<uint32_t>(intAt(args, 0))].known);
    else if (op == "player.setSkill")
    {
        FakeSkill& s = p.skills[static_cast<uint32_t>(intAt(args, 0))];
        s.known = true;
        s.step = static_cast<uint32_t>(intAt(args, 1));
        s.cur = static_cast<uint32_t>(intAt(args, 2));
        s.max = static_cast<uint32_t>(intAt(args, 3));
    }
    // ---- cooldowns ----
    else if (op == "player.resetSpellCooldown")
    {
        uint32_t const spellId = static_cast<uint32_t>(intAt(args, 0));
        p.resetSpells.emplace_back(spellId, boolAt(args, 1, true));
        p.spellCooldowns.erase(spellId);
        p.spellCooldownDelay.erase(spellId);
    }
    else if (op == "player.hasSpellCooldown")
        out.emplace_back(p.spellCooldowns.count(static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "player.getSpellCooldownDelay")
        out.emplace_back(static_cast<int64_t>(p.spellCooldownDelay[static_cast<uint32_t>(intAt(args, 0))]));
    else if (op == "player.resetAllCooldowns")
    {
        ++p.resetAll;
        p.spellCooldowns.clear();
        p.spellCooldownDelay.clear();
    }
    else if (op == "player.resetTypeCooldowns")
    {
        p.resetTypes.emplace_back(static_cast<uint32_t>(intAt(args, 0)), boolAt(args, 1, true));
        p.spellCooldowns.clear();
        p.spellCooldownDelay.clear();
    }
    // ---- items ----
    else if (op == "player.getItemByPos")
    {
        uint32_t const bag = static_cast<uint32_t>(intAt(args, 0));
        uint32_t const slot = static_cast<uint32_t>(intAt(args, 1));
        for (FakeItem const& it : p.items)
            if (it.bag == bag && it.slot == slot)
            {
                pushItem(it);
                break;
            }
    }
    else if (op == "player.getItemByEntry")
    {
        if (FakeItem* it = findItemByEntry(static_cast<uint32_t>(intAt(args, 0))))
            pushItem(*it);
    }
    else if (op == "player.getItemByGUID")
    {
        fl::Guid g;
        if (fl::parseGuidText(stringAt(args, 0), g))
            if (FakeItem* it = findItemByLow(g.lo))
                pushItem(*it);
    }
    else if (op == "player.getEquippedItemBySlot")
    {
        uint32_t const slot = static_cast<uint32_t>(intAt(args, 0));
        for (FakeItem const& it : p.items)
            if (it.bag == 0 && it.slot == slot && p.equipped.count(slot))
            {
                pushItem(it);
                break;
            }
    }
    else if (op == "player.equipItem")
    {
        uint32_t const slot = static_cast<uint32_t>(intAt(args, 1));
        fl::HandleData h;
        if (handleAt(args, 0, h))
        {
            if (FakeItem* it = findItemByLow(h.guid.lo))
            {
                it->bag = 0;
                it->slot = slot;
                p.equipped.insert(slot);
                pushItem(*it);
            }
        }
        else
        {
            FakeItem it;
            it.low = p.nextItemGuid++;
            it.entry = static_cast<uint32_t>(intAt(args, 0));
            it.bag = 0;
            it.slot = slot;
            p.items.push_back(it);
            p.equipped.insert(slot);
            pushItem(it);
        }
    }
    else if (op == "player.canEquipItem")
        out.emplace_back(p.canEquip);
    else if (op == "player.canUseItem")
        out.emplace_back(p.canUse);
    // ---- quests ----
    else if (op == "player.getQuestRewardStatus")
        out.emplace_back(p.rewarded.count(static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "player.incompleteQuest")
    {
        p.activeQuests.insert(static_cast<uint32_t>(intAt(args, 0)));
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.failQuest")
    {
        p.activeQuests.erase(static_cast<uint32_t>(intAt(args, 0)));
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.removeQuest")
    {
        uint32_t const q = static_cast<uint32_t>(intAt(args, 0));
        p.activeQuests.erase(q);
        p.rewarded.erase(q);
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.removeActiveQuest")
    {
        p.activeQuests.erase(static_cast<uint32_t>(intAt(args, 0)));
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.removeRewardedQuest")
    {
        p.rewarded.erase(static_cast<uint32_t>(intAt(args, 0)));
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.canCompleteQuest")
        out.emplace_back(p.completed.count(static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "player.canRewardQuest")
        out.emplace_back(p.canReward);
    else if (op == "player.rewardQuest")
    {
        p.rewarded.insert(static_cast<uint32_t>(intAt(args, 0)));
        p.activeQuests.erase(static_cast<uint32_t>(intAt(args, 0)));
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.areaExploredOrEventHappens" || op == "player.killedMonsterCredit" ||
             op == "player.groupEventHappens" || op == "player.talkedToCreature")
        p.actions.emplace_back(op, args);
    else if (op == "player.hasQuestForGO")
        out.emplace_back(p.hasQuestForGO);
    else if (op == "player.hasQuestForItem")
        out.emplace_back(p.hasQuestForItem);
    else if (op == "player.getReqKillOrCastCurrentCount")
        out.emplace_back(static_cast<int64_t>(p.reqKillCount));
    else if (op == "player.hasReceivedQuestReward")
        out.emplace_back(p.rewarded.count(static_cast<uint32_t>(intAt(args, 0))) != 0);
    // ---- state ----
    else if (op == "player.isInGroup")
        out.emplace_back(p.inGroup);
    else if (op == "player.isInGuild")
        out.emplace_back(p.inGuild);
    else if (op == "player.isFlying")
        out.emplace_back(p.flying);
    else if (op == "player.isMoving")
        out.emplace_back(p.moving);
    else if (op == "player.isFalling")
        out.emplace_back(p.falling);
    else if (op == "player.isInWater")
        out.emplace_back(p.inWater);
    else if (op == "player.isTaxiCheater")
        out.emplace_back(p.taxiCheat);
    else if (op == "player.setTaxiCheat")
        p.taxiCheat = boolAt(args, 0, true);
    else if (op == "player.inBattleground")
        out.emplace_back(p.inBg);
    else if (op == "player.getBattlegroundId")
        out.emplace_back(static_cast<int64_t>(p.bgId));
    else if (op == "player.getBattlegroundTypeId")
        out.emplace_back(static_cast<int64_t>(p.bgTypeId));
    else if (op == "player.isAFK")
        out.emplace_back(p.afk);
    else if (op == "player.isDND")
        out.emplace_back(p.dnd);
    else if (op == "player.toggleAFK")
        p.afk = !p.afk;
    else if (op == "player.toggleDND")
        p.dnd = !p.dnd;
    else if (op == "player.setFFA")
        p.ffa = boolAt(args, 0, true);
    else if (op == "player.isImmuneToDamage")
        out.emplace_back(p.immune);
    else if (op == "player.isImmuneToEnvironmentalDamage")
        out.emplace_back(p.envImmune);
    else if (op == "unit.setPvP")
        p.pvp = boolAt(args, 0, true);
    // ---- chat and UI sends ----
    else if (op == "player.say" || op == "player.yell" || op == "player.whisper" || op == "player.textEmote" ||
             op == "player.sendShowBank" || op == "player.sendListInventory" || op == "player.sendTrainerList" ||
             op == "player.sendTaxiMenu" || op == "player.summonPlayer")
        p.actions.emplace_back(op, args);
    // ---- bind, misc ----
    else if (op == "player.setBindPoint")
    {
        p.bindSet = true;
        p.bindX = doubleAt(args, 0);
        p.bindY = doubleAt(args, 1);
        p.bindZ = doubleAt(args, 2);
        p.bindMap = static_cast<uint32_t>(intAt(args, 3));
        p.bindArea = static_cast<uint32_t>(intAt(args, 4));
    }
    else if (op == "player.getGuildName")
        out.emplace_back(p.guildName);
    else if (op == "player.getDbLocaleIndex")
        out.emplace_back(static_cast<int64_t>(p.dbLocale));
    else if (op == "player.getPlayerIP")
        out.emplace_back(p.ip);
    else if (op == "player.getLatency")
        out.emplace_back(static_cast<int64_t>(p.latency));
    else if (op == "player.getTotalPlayedTime")
        out.emplace_back(static_cast<int64_t>(p.totalPlayed));
    else if (op == "player.getLevelPlayedTime")
        out.emplace_back(static_cast<int64_t>(p.levelPlayed));
    else if (op == "player.logoutPlayer")
    {
        p.loggedOut = true;
        p.logoutSave = boolAt(args, 0, true);
    }
    else if (op == "player.durabilityRepairAll")
    {
        p.repaired = true;
        p.repairTakeCost = boolAt(args, 0, true);
        p.repairDiscount = doubleAt(args, 1, 1.0);
        p.repairGuildBank = boolAt(args, 2, false);
    }
    // ---- E7 long tail, round 2: the documented reads ----
    else if (op == "player.canBlock")
        out.emplace_back(p.canBlock);
    else if (op == "player.canCompleteRepeatableQuest")
        out.emplace_back(p.canCompleteRepeatable);
    else if (op == "player.canFly")
        out.emplace_back(p.canFly);
    else if (op == "player.canParry")
        out.emplace_back(p.canParry);
    else if (op == "player.canShareQuest")
        out.emplace_back(p.canShare);
    else if (op == "player.canSpeak")
        out.emplace_back(p.canSpeak);
    else if (op == "player.canUninviteFromGroup")
        out.emplace_back(static_cast<int64_t>(p.partyResult));
    else if (op == "player.getChatTag")
        out.emplace_back(static_cast<int64_t>(flh::chatTagBits(p.afk, p.dnd, p.gmChat)));
    else if (op == "player.getChampioningFaction")
        out.emplace_back(static_cast<int64_t>(p.championing));
    else if (op == "player.getDbcLocale")
        out.emplace_back(static_cast<int64_t>(p.dbcLocale));
    else if (op == "player.getDifficulty")
        out.emplace_back(static_cast<int64_t>(p.difficulty));
    else if (op == "player.getDrunkValue")
        out.emplace_back(static_cast<int64_t>(p.drunk));
    else if (op == "player.getGroupInvite")
    {
        if (p.groupInvite)
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::Group;
            h.guid.lo = p.groupInvite;
            out.emplace_back(h);
        }
    }
    else if (op == "player.getGuildRank")
        out.emplace_back(static_cast<int64_t>(p.guildRank));
    else if (op == "player.getHealthBonusFromStamina")
        out.emplace_back(p.healthBonus);
    else if (op == "player.getInGameTime")
        out.emplace_back(static_cast<int64_t>(p.inGameTime));
    else if (op == "player.getLifetimeKills")
        out.emplace_back(static_cast<int64_t>(p.lifetimeKills));
    else if (op == "player.getMailCount")
        out.emplace_back(static_cast<int64_t>(p.mailCount));
    else if (op == "player.getMailItem")
    {
        fl::Guid g;
        if (fl::parseGuidText(stringAt(args, 0), g) && p.mailedItems.count(g.lo))
            if (FakeItem* it = findItemByLow(g.lo))
                pushItem(*it);
    }
    else if (op == "player.getManaBonusFromIntellect")
        out.emplace_back(p.manaBonus);
    else if (op == "player.getNearbyGameObject")
    {
        if (p.nearbyGameObject)
            out.emplace_back(gameObject(p.nearbyGameObject));
    }
    else if (op == "player.getNextRandomRaidMember")
    {
        if (p.nextRaidMember)
            out.emplace_back(player(p.nextRaidMember));
    }
    else if (op == "player.getOriginalGroup")
    {
        if (p.originalGroup)
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::Group;
            h.guid.lo = p.originalGroup;
            out.emplace_back(h);
        }
    }
    else if (op == "player.getOriginalSubGroup")
        out.emplace_back(static_cast<int64_t>(p.originalSubGroup));
    else if (op == "player.getQuestLevel")
        out.emplace_back(static_cast<int64_t>(p.questLevel));
    else if (op == "player.getRecruiterId")
        out.emplace_back(static_cast<int64_t>(p.recruiterId));
    else if (op == "player.getSelectedPlayer")
    {
        if (p.selectedPlayer)
            out.emplace_back(player(p.selectedPlayer));
    }
    else if (op == "player.getSelectedUnit")
    {
        if (p.selectedUnitCreature)
            out.emplace_back(creature(p.selectedUnitCreature));
    }
    else if (op == "player.getShieldBlockValue")
        out.emplace_back(static_cast<int64_t>(p.shieldBlockValue));
    else if (op == "player.getSubGroup")
        out.emplace_back(static_cast<int64_t>(p.subGroup));
    else if (op == "player.getXPRestBonus")
        out.emplace_back(static_cast<int64_t>(p.xpRestBonus));
    else if (op == "player.hasAtLoginFlag")
        out.emplace_back((p.atLoginFlags & static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "player.hasPendingBind")
        out.emplace_back(p.pendingBind);
    else if (op == "player.hasTalent")
        out.emplace_back(p.talents.count({static_cast<uint32_t>(intAt(args, 0)),
                                          static_cast<uint8_t>(intAt(args, 1))}) != 0);
    else if (op == "player.hasTitle")
        out.emplace_back(p.titles.count(static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "player.inArena")
        out.emplace_back(p.inArena);
    else if (op == "player.inBattlegroundQueue")
        out.emplace_back(p.inBgQueue);
    else if (op == "player.isAcceptingWhispers")
        out.emplace_back(p.acceptWhispers);
    else if (op == "player.isARecruiter")
        out.emplace_back(p.isRecruiter);
    else if (op == "player.isGMChat")
        out.emplace_back(p.gmChat);
    else if (op == "player.isGMVisible")
        out.emplace_back(p.gmVisible);
    else if (op == "player.isHonorOrXPTarget")
        out.emplace_back(p.honorXpTarget);
    else if (op == "player.isGroupVisibleFor" || op == "player.isInSameGroupWith" || op == "player.isInSameRaidWith")
    {
        fl::HandleData other;
        if (handleAt(args, 0, other))
        {
            auto const& mates = op == "player.isGroupVisibleFor" ? p.visibleFor
                               : op == "player.isInSameGroupWith" ? p.groupMates
                                                                  : p.raidMates;
            out.emplace_back(mates.count(other.guid.lo) != 0);
        }
    }
    else if (op == "player.isNeverVisible")
        out.emplace_back(p.neverVisible);
    else if (op == "player.isOutdoorPvPActive")
        out.emplace_back(p.outdoorPvp);
    else if (op == "player.isVisibleForPlayer")
        out.emplace_back(p.seesPlayer);
    else if (op == "player.resetTalentsCost")
        out.emplace_back(static_cast<int64_t>(p.resetTalentsCost));
    // ---- E7 long tail, round 2: the writes ----
    else if (op == "player.sendCinematicStart" || op == "player.sendMovieStart" ||
             op == "player.sendUpdateWorldState" || op == "player.sendSpiritResurrect")
        p.actions.emplace_back(op, args);
    else if (op == "player.setAcceptWhispers")
        p.acceptWhispers = boolAt(args, 0, true);
    else if (op == "player.setAtLoginFlag")
        p.atLoginFlags |= static_cast<uint32_t>(intAt(args, 0));
    else if (op == "player.setDrunkValue")
        p.drunk = static_cast<uint8_t>(intAt(args, 0));
    else if (op == "player.setFactionForRace")
    {
        uint32_t const race = static_cast<uint32_t>(intAt(args, 0));
        if (race < 1 || race > 11)
        {
            out.emplace_back(false);
            return true;
        }
        p.factionRace = static_cast<uint8_t>(race);
    }
    else if (op == "player.setGameMaster")
        p.gameMaster = boolAt(args, 0, false);
    else if (op == "player.setGender")
        p.gender = static_cast<uint8_t>(intAt(args, 0));
    else if (op == "player.setGMChat")
        p.gmChat = boolAt(args, 0, false);
    else if (op == "player.setGMVisible")
        p.gmVisible = boolAt(args, 0, true);
    else if (op == "player.setPvPDeath")
        p.pvpDeath = boolAt(args, 0, false);
    else if (op == "player.spawnBones")
        p.bonesSpawned = true;
    else if (op == "player.killPlayer")
        p.killed = true;
    else if (op == "player.resetTalents")
    {
        bool const noCost = boolAt(args, 0, false);
        int64_t const cost = noCost ? 0 : static_cast<int64_t>(p.resetTalentsCost);
        if (!noCost && p.money < cost)
        {
            out.emplace_back(false);
            return true;
        }
        ++p.resetTalentsCalls;
        p.resetTalentsNoCost = noCost;
        if (p.resetTalentsResult && !noCost)
            p.money -= cost;
        out.emplace_back(p.resetTalentsResult);
    }
    else if (op == "player.durabilityLoss" || op == "player.durabilityPointsLoss")
    {
        FakeDurability call;
        call.op = op;
        fl::HandleData item;
        if (handleAt(args, 0, item))
            call.itemLow = item.guid.lo;
        // The module resolves the item through the receiver's own inventory, so a handle of another player's item
        // reaches nothing here either.
        bool owned = false;
        for (FakeItem const& it : p.items)
            if (it.low == call.itemLow)
            {
                owned = true;
                break;
            }
        if (!owned)
            return true;
        if (op == "player.durabilityLoss")
            call.percent = doubleAt(args, 1);
        else
            call.points = intAt(args, 1);
        p.durabilities.push_back(call);
    }
    else if (op == "player.durabilityLossAll" || op == "player.durabilityPointsLossAll")
    {
        FakeDurability call;
        call.op = op;
        if (op == "player.durabilityLossAll")
            call.percent = doubleAt(args, 0);
        else
            call.points = intAt(args, 0);
        call.inventory = boolAt(args, 1, true);
        p.durabilities.push_back(call);
    }
    else if (op == "player.durabilityPointLossForEquipSlot")
    {
        FakeDurability call;
        call.op = op;
        call.slot = intAt(args, 0);
        p.durabilities.push_back(call);
    }
    else if (op == "player.durabilityRepair")
    {
        FakeDurability call;
        call.op = op;
        call.pos = intAt(args, 0);
        call.takeCost = boolAt(args, 1, true);
        call.discount = doubleAt(args, 2, 1.0);
        p.durabilities.push_back(call);
    }
    // ---- E7 long tail, round 3: remaining Player methods ----
    else if (op == "player.canTitanGrip")
        out.emplace_back(p.canTitanGrip);
    else if (op == "player.getComboPoints")
        out.emplace_back(static_cast<int64_t>(p.comboPoints));
    else if (op == "player.getComboTarget")
    {
        if (p.comboTargetLow)
            out.emplace_back(creature(p.comboTargetLow));
    }
    else if (op == "player.getGossipTextId")
        out.emplace_back(static_cast<int64_t>(p.gossipTextId));
    else if (op == "player.hasRecruited")
    {
        fl::HandleData recruit;
        bool has = false;
        if (handleAt(args, 0, recruit))
            has = p.recruitedPlayers.count(recruit.guid.lo) != 0;
        out.emplace_back(has);
    }
    else if (op == "player.isRecruited")
        out.emplace_back(p.recruited);
    else if (op == "player.addComboPoints")
    {
        fl::HandleData target;
        if (handleAt(args, 0, target))
        {
            int64_t const count = intAt(args, 1);
            if (count > 0 && p.comboTargetLow != target.guid.lo)
            {
                p.comboPoints = 0;
                p.comboTargetLow = target.guid.lo;
            }
            if (count != 0 && (count > 0 || p.comboTargetLow == target.guid.lo))
            {
                int64_t const next = std::clamp<int64_t>(static_cast<int64_t>(p.comboPoints) + count, 0, 5);
                p.comboPoints = static_cast<uint8_t>(next);
                if (p.comboPoints == 0)
                    p.comboTargetLow = 0;
            }
        }
    }
    else if (op == "player.clearComboPoints")
    {
        p.comboPoints = 0;
        p.comboTargetLow = 0;
    }
    else if (op == "player.addTalent")
    {
        uint32_t const spellId = static_cast<uint32_t>(intAt(args, 0));
        uint8_t const spec = static_cast<uint8_t>(intAt(args, 1));
        if (p.addTalentResult && spec < 5)
            p.talents.insert({spellId, spec});
        out.emplace_back(p.addTalentResult && spec < 5);
    }
    else if (op == "player.advanceSkill")
    {
        FakeSkill& s = p.skills[static_cast<uint32_t>(intAt(args, 0))];
        uint32_t const step = flh::skillAdvanceStep(s.cur, s.max, static_cast<uint32_t>(intAt(args, 1)));
        if (s.known && s.max > 0 && step > 0)
        {
            // The core's UpdateSkillPro adds the step to a 16-bit value, then clamps (Player.cpp).
            s.cur = static_cast<uint16_t>(s.cur + step);
            if (s.cur > s.max)
                s.cur = s.max;
        }
    }
    else if (op == "player.advanceAllSkills")
    {
        uint32_t const requested = static_cast<uint32_t>(intAt(args, 0));
        for (auto& [id, s] : p.skills)
        {
            uint32_t const step = flh::skillAdvanceStep(s.cur, s.max, requested);
            if (s.known && s.max > 0 && step > 0)
            {
                s.cur = static_cast<uint16_t>(s.cur + step);
                if (s.cur > s.max)
                    s.cur = s.max;
            }
        }
    }
    else if (op == "player.advanceSkillsToMax")
    {
        for (uint32_t id : p.weaponSkills)
        {
            FakeSkill& s = p.skills[id];
            if (s.known && s.max > s.cur)
                s.cur = s.max;
        }
    }
    else if (op == "player.bindToInstance")
    {
        if (flh::bindToInstanceAllowed(true, p.instanceHasData, p.instanceHasLock))
            p.instanceBound = true;
    }
    else if (op == "player.unbindInstance")
        p.instanceBinds.erase({static_cast<uint32_t>(intAt(args, 0)), static_cast<uint32_t>(intAt(args, 1, 0))});
    else if (op == "player.unbindAllInstances")
        p.instanceBinds.clear();
    else if (op == "player.mute")
        p.muteTime = static_cast<uint32_t>(intAt(args, 0));
    else if (op == "player.removeFromBattlegroundRaid")
        p.inBgRaid = false;
    else if (op == "player.removeFromGroup")
        p.inGroup = false;
    else if (op == "player.removePet")
    {
        p.activePetEntry = 0;
        p.removedPetMode = static_cast<int32_t>(intAt(args, 0, -1));
        p.removedPetReagent = boolAt(args, 1, false);
    }
    else if (op == "player.summonPet")
    {
        p.activePetEntry = static_cast<uint32_t>(intAt(args, 0));
        p.activePetType = static_cast<uint32_t>(intAt(args, 5));
        p.actions.emplace_back(op, args);
    }
    else if (op == "player.setKnownTitle")
        p.titles.insert(static_cast<uint32_t>(intAt(args, 0)));
    else if (op == "player.unsetKnownTitle")
        p.titles.erase(static_cast<uint32_t>(intAt(args, 0)));
    else if (op == "player.setMovement")
        p.movementType = static_cast<int32_t>(intAt(args, 0));
    else if (op == "player.setPlayerLock")
        p.playerLock = boolAt(args, 0, true);
    else if (op == "player.setQuestStatus")
    {
        uint32_t const quest = static_cast<uint32_t>(intAt(args, 0));
        if (flh::questStatusWriteAllowed(p.questLog.count(quest) != 0, intAt(args, 1), 7))
            p.questStatus[quest] = static_cast<uint32_t>(intAt(args, 1));
    }
    else if (op == "player.startTaxi")
        p.taxiPath = static_cast<uint32_t>(intAt(args, 0));
    else if (op == "player.groupCreate")
    {
        if (p.createdGroupId)
        {
            fl::HandleData h;
            h.kind = fl::ObjKind::Group;
            h.guid.hi = 1;
            h.guid.lo = p.createdGroupId;
            out.emplace_back(h);
        }
    }
    else if (op == "player.groupInvite")
        out.emplace_back(p.groupInviteResult);
    else if (op == "player.setGuildRank")
    {
        if (flh::guildRankWriteAllowed(static_cast<uint32_t>(intAt(args, 0))))
            p.guildRank = static_cast<uint8_t>(intAt(args, 0));
    }
    else if (op == "player.runCommand")
        p.playerCommands.push_back(stringAt(args, 0));
    else if (op == "player.gossipSendPOI" || op == "player.killedPlayerCredit" || op == "player.killGOCredit" ||
             op == "player.leaveBattleground" || op == "player.removedInsignia" || op == "player.sendAddonMessage" ||
             op == "player.sendAuctionMenu" || op == "player.sendGuildInvite" || op == "player.sendQuestTemplate" ||
             op == "player.sendShowMailBox" || op == "player.sendTabardVendorActivate")
        p.actions.emplace_back(op, args);
    return true;
}

bool FakeHost::guidInfo(fl::Guid const& g, uint32_t* entry, int* type)
{
    // The high half carries the type; a zero high half is the legacy low-counter form and decodes to nothing.
    uint64_t const t = g.hi >> 58;
    if (type)
        *type = g.hi == 0 ? -1 : static_cast<int>(t);
    if (entry)
        *entry = (t == kTypeCreature || t == kTypeGameObject) ? static_cast<uint32_t>((g.hi >> 20) & 0xFFFFFF) : 0;
    return true;
}

fl::Guid FakeHost::buildGuid(fl::ObjKind kind, uint64_t low, uint32_t entry, uint16_t mapId)
{
    switch (kind)
    {
        case fl::ObjKind::Player: return makeGuid(kTypePlayer, low, 0);
        case fl::ObjKind::Item: return makeGuid(kTypeItem, low, 0);
        case fl::ObjKind::Creature: return makeGuid(kTypeCreature, low, entry, mapId);
        case fl::ObjKind::GameObject: return makeGuid(kTypeGameObject, low, entry, mapId);
        default: return fl::Guid{};
    }
}

bool FakeHost::isInventoryPos(int kind, uint8_t bag, uint8_t slot)
{
    constexpr uint8_t kBag0 = 255;
    switch (kind)
    {
        case 0:   // IsBagPos
            return bag == kBag0 && ((slot >= 30 && slot < 34) || (slot >= 63 && slot < 72) || (slot >= 34 && slot < 35));
        case 1:   // IsBankPos
            return (bag == kBag0 && slot >= 63 && slot < 72) || (bag >= 63 && bag < 72);
        case 2:   // IsEquipmentPos
            return bag == kBag0 && (slot < 19 || (slot >= 19 && slot < 30) || (slot >= 30 && slot < 34) ||
                                    (slot >= 34 && slot < 35));
        case 3:   // IsInventoryPos
            return (bag == kBag0 && (slot == 255 || (slot >= 35 && slot < 63) || (slot >= 84 && slot < 87))) ||
                   (bag >= 30 && bag < 34) || (bag >= 34 && bag < 35);
        default: return false;
    }
}

bool FakeHost::playerOwner(fl::HandleData const& h, uint64_t& owner)
{
    owner = 0;
    if (h.kind != fl::ObjKind::Player)
        return false;
    auto it = playerOwners.find({h.guid.hi, h.guid.lo});
    if (it == playerOwners.end())
        return false;
    owner = it->second;
    return owner != 0;
}

bool FakeHost::playerByName(std::string const& name, fl::HandleData& out)
{
    auto it = playersByName.find(name);
    if (it == playersByName.end())
        return false;
    out = it->second;
    return true;
}

bool FakeHost::playerByGuid(fl::Guid const& guid, fl::HandleData& out)
{
    for (auto const& p : players)
        if (p.guid == guid || (guid.hi == 0 && p.guid.lo == guid.lo))   // the full guid, or the legacy low counter
        {
            out = p;
            return true;
        }
    return false;
}

std::vector<fl::HandleData> FakeHost::playersOnMap(uint32_t mapId, uint32_t instanceId, int team, bool onlyGm)
{
    noteMapLock();
    (void)team;
    (void)onlyGm;
    std::vector<fl::HandleData> out;
    for (auto const& p : players)
    {
        uint32_t m = 0, i = 0;
        if (currentMap(p, m, i) && m == mapId && i == instanceId)
            out.push_back(p);
    }
    return out;
}

std::vector<fl::HandleData> FakeHost::playersInWorld(int team, bool onlyGm)
{
    (void)team;
    (void)onlyGm;
    return players;
}

namespace
{
constexpr double kFakePi = 3.14159265358979323846;

double normAngle(double a)
{
    double r = std::fmod(a, 2.0 * kFakePi);
    return r < 0.0 ? r + 2.0 * kFakePi : r;
}

fl::HandleData handleFromObjKey(std::tuple<int, uint64_t, uint64_t> const& key, flh::MapKey const& map)
{
    fl::HandleData h;
    h.kind = static_cast<fl::ObjKind>(std::get<0>(key));
    h.guid.hi = std::get<1>(key);
    h.guid.lo = std::get<2>(key);
    h.mapId = map.map;
    h.instanceId = map.instance;
    return h;
}
}  // namespace

std::vector<fl::HandleData> FakeHost::objectsInRange(fl::HandleData const& self, int kind, float range, uint32_t entry,
                                                     int hostile, int dead)
{
    range = flh::clampSearchRange(range);
    std::vector<std::pair<double, fl::HandleData>> found;
    auto selfIt = _objects.find(keyOf(self));
    if (selfIt == _objects.end())
        return {};
    fl::ObjectInfo const& me = selfIt->second.info;
    flh::MapKey const myMap = selfIt->second.current;
    double const myReach = woState(self).combatReach;
    for (auto const& [k, obj] : _objects)
    {
        if (k == keyOf(self) || obj.current != myMap)
            continue;
        auto const objKind = static_cast<fl::ObjKind>(std::get<0>(k));
        if (kind == 0 && objKind != fl::ObjKind::Player)
            continue;
        if (kind == 1 && objKind != fl::ObjKind::Creature)
            continue;
        if (kind == 2 && objKind != fl::ObjKind::GameObject)
            continue;
        if (entry && obj.info.entry != entry)
            continue;
        fl::HandleData const h = handleFromObjKey(k, obj.current);
        FakeWorldObject const& fwo = woState(h);
        if (hostile == 1 && !fwo.hostileToAll)
            continue;
        if (hostile == 2 && !fwo.friendlyToAll)
            continue;
        if (objKind == fl::ObjKind::Player || objKind == fl::ObjKind::Creature)
        {
            if (dead == 1 && !obj.info.alive)
                continue;
            if (dead == 2 && obj.info.alive)
                continue;
        }
        double const dx = obj.info.x - me.x;
        double const dy = obj.info.y - me.y;
        double const dz = obj.info.z - me.z;
        double const d = std::max(0.0, std::sqrt(dx * dx + dy * dy + dz * dz) - myReach - fwo.combatReach);
        if (d <= static_cast<double>(range))
            found.emplace_back(d, h);
    }
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<fl::HandleData> out;
    out.reserve(found.size());
    for (auto const& item : found)
        out.push_back(item.second);
    return out;
}

bool FakeHost::worldObjectStateCall(fl::HandleData const& self, std::string const& op,
                                    std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    static std::set<std::string> const ops = {
        "worldobject.getAngle",        "worldobject.getDistance2d",    "worldobject.getExactDistance",
        "worldobject.getExactDistance2d", "worldobject.getRelativePoint", "worldobject.isInFront",
        "worldobject.isInBack",        "worldobject.isInMap",          "worldobject.isInRange",
        "worldobject.isInRange2d",     "worldobject.isInRange3d",      "worldobject.isWithinDist",
        "worldobject.isWithinDist2d",  "worldobject.isWithinDist3d",   "worldobject.isWithinLoS",
        "worldobject.getNearObjects",  "worldobject.getNearObject",    "worldobject.playDirectSound",
        "worldobject.playDistanceSound", "worldobject.playMusic",      "worldobject.say",
        "worldobject.yell",            "worldobject.whisper",          "unit.say",
        "unit.yell",                   "unit.whisper"};
    if (!ops.count(op))
        return false;

    auto selfIt = _objects.find(keyOf(self));
    if (selfIt == _objects.end())
        return true;
    fl::ObjectInfo const& me = selfIt->second.info;
    flh::MapKey const myMap = selfIt->second.current;
    FakeWorldObject& fwo = woState(self);

    auto resolveOther = [&](size_t idx, fl::ObjectInfo& info, flh::MapKey& map, double& reach) -> bool
    {
        fl::HandleData h;
        if (!handleAt(args, idx, h))
            return false;
        auto it = _objects.find(keyOf(h));
        if (it == _objects.end())
            return false;
        info = it->second.info;
        map = it->second.current;
        reach = woState(h).combatReach;
        return true;
    };

    if (op == "worldobject.getAngle")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        double const tx = resolveOther(0, o, m, r) ? o.x : doubleAt(args, 0);
        double const ty = resolveOther(0, o, m, r) ? o.y : doubleAt(args, 1);
        out.emplace_back(normAngle(std::atan2(ty - me.y, tx - me.x)));
    }
    else if (op == "worldobject.getDistance2d")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        bool const hasOther = resolveOther(0, o, m, r);
        double const tx = hasOther ? o.x : doubleAt(args, 0);
        double const ty = hasOther ? o.y : doubleAt(args, 1);
        double const d = std::hypot(tx - me.x, ty - me.y) - fwo.combatReach - (hasOther ? r : 0.0);
        out.emplace_back(std::max(0.0, d));
    }
    else if (op == "worldobject.getExactDistance")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        bool const hasOther = resolveOther(0, o, m, r);
        double const tx = hasOther ? o.x : doubleAt(args, 0);
        double const ty = hasOther ? o.y : doubleAt(args, 1);
        double const tz = hasOther ? o.z : doubleAt(args, 2);
        double const dx = tx - me.x, dy = ty - me.y, dz = tz - me.z;
        out.emplace_back(std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    else if (op == "worldobject.getExactDistance2d")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        bool const hasOther = resolveOther(0, o, m, r);
        double const tx = hasOther ? o.x : doubleAt(args, 0);
        double const ty = hasOther ? o.y : doubleAt(args, 1);
        out.emplace_back(std::hypot(tx - me.x, ty - me.y));
    }
    else if (op == "worldobject.getRelativePoint")
    {
        double const dist = doubleAt(args, 0);
        double const angle = me.o + doubleAt(args, 1);
        out.emplace_back(me.x + (fwo.combatReach + dist) * std::cos(angle));
        out.emplace_back(me.y + (fwo.combatReach + dist) * std::sin(angle));
        out.emplace_back(static_cast<double>(me.z));
    }
    else if (op == "worldobject.isInFront" || op == "worldobject.isInBack")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        if (!resolveOther(0, o, m, r))
            return true;
        double const arc = doubleAt(args, 1, kFakePi);
        auto inArc = [&](double a)
        {
            double const d2 = std::hypot(o.x - me.x, o.y - me.y);
            if (d2 < 2.0)
                return true;
            double diff = std::fabs(normAngle(std::atan2(o.y - me.y, o.x - me.x) - me.o));
            if (diff > kFakePi)
                diff = 2.0 * kFakePi - diff;
            return diff <= a * 0.5;
        };
        out.emplace_back(op == "worldobject.isInFront" ? inArc(arc) : !inArc(2.0 * kFakePi - arc));
    }
    else if (op == "worldobject.isInMap")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        if (!resolveOther(0, o, m, r))
            return true;
        out.emplace_back(myMap == m);
    }
    else if (op == "worldobject.isInRange")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        if (!resolveOther(0, o, m, r))
            return true;
        double const minR = doubleAt(args, 1), maxR = doubleAt(args, 2);
        bool const is3d = boolAt(args, 3, true);
        double const dx = o.x - me.x, dy = o.y - me.y, dz = is3d ? (o.z - me.z) : 0.0;
        double const dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        double const sf = fwo.combatReach + r;
        bool const ok = (minR <= 0.0 || dist >= minR + sf) && dist < maxR + sf;
        out.emplace_back(ok);
    }
    else if (op == "worldobject.isInRange2d")
    {
        double const dist = std::hypot(doubleAt(args, 0) - me.x, doubleAt(args, 1) - me.y);
        double const minR = doubleAt(args, 2), maxR = doubleAt(args, 3);
        bool const ok = (minR <= 0.0 || dist >= minR + fwo.combatReach) && dist < maxR + fwo.combatReach;
        out.emplace_back(ok);
    }
    else if (op == "worldobject.isInRange3d")
    {
        double const dx = doubleAt(args, 0) - me.x, dy = doubleAt(args, 1) - me.y, dz = doubleAt(args, 2) - me.z;
        double const dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        double const minR = doubleAt(args, 3), maxR = doubleAt(args, 4);
        bool const ok = (minR <= 0.0 || dist >= minR + fwo.combatReach) && dist < maxR + fwo.combatReach;
        out.emplace_back(ok);
    }
    else if (op == "worldobject.isWithinDist")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        if (!resolveOther(0, o, m, r))
            return true;
        double const maxDist = doubleAt(args, 1) + fwo.combatReach + r;
        bool const is3d = boolAt(args, 2, true);
        double const dx = o.x - me.x, dy = o.y - me.y, dz = is3d ? (o.z - me.z) : 0.0;
        out.emplace_back((dx * dx + dy * dy + dz * dz) < maxDist * maxDist);
    }
    else if (op == "worldobject.isWithinDist2d")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        bool const hasOther = resolveOther(0, o, m, r);
        double const tx = hasOther ? o.x : doubleAt(args, 0);
        double const ty = hasOther ? o.y : doubleAt(args, 1);
        double const maxDist = (hasOther ? doubleAt(args, 1) : doubleAt(args, 2)) + fwo.combatReach;
        double const dx = tx - me.x, dy = ty - me.y;
        out.emplace_back((dx * dx + dy * dy) < maxDist * maxDist);
    }
    else if (op == "worldobject.isWithinDist3d")
    {
        fl::ObjectInfo o;
        flh::MapKey m;
        double r = 0.0;
        bool const hasOther = resolveOther(0, o, m, r);
        double const tx = hasOther ? o.x : doubleAt(args, 0);
        double const ty = hasOther ? o.y : doubleAt(args, 1);
        double const tz = hasOther ? o.z : doubleAt(args, 2);
        double const maxDist = (hasOther ? doubleAt(args, 1) : doubleAt(args, 3)) + fwo.combatReach;
        double const dx = tx - me.x, dy = ty - me.y, dz = tz - me.z;
        out.emplace_back((dx * dx + dy * dy + dz * dz) < maxDist * maxDist);
    }
    else if (op == "worldobject.isWithinLoS")
    {
        out.emplace_back(fwo.losOk);
    }
    else if (op == "worldobject.getNearObjects" || op == "worldobject.getNearObject")
    {
        double const range = static_cast<double>(flh::clampSearchRange(doubleAt(args, 0, 533.33333)));
        uint32_t const typeMask = static_cast<uint32_t>(intAt(args, 1, 0));
        uint32_t const entry = static_cast<uint32_t>(intAt(args, 2, 0));
        int const hostile = static_cast<int>(intAt(args, 3, 0));
        int const dead = static_cast<int>(intAt(args, 4, 1));
        std::vector<std::pair<double, fl::HandleData>> found;
        for (auto const& [k, obj] : _objects)
        {
            if (k == keyOf(self) || obj.current != myMap)
                continue;
            auto const objKind = static_cast<fl::ObjKind>(std::get<0>(k));
            if (typeMask)
            {
                bool match = false;
                if ((typeMask & (1u << 3)) && (objKind == fl::ObjKind::Creature || objKind == fl::ObjKind::Player))
                    match = true;
                if ((typeMask & (1u << 4)) && objKind == fl::ObjKind::Player)
                    match = true;
                if ((typeMask & (1u << 5)) && objKind == fl::ObjKind::GameObject)
                    match = true;
                if ((typeMask & (1u << 7)) && objKind == fl::ObjKind::Corpse)
                    match = true;
                if (!match)
                    continue;
            }
            if (entry && obj.info.entry != entry)
                continue;
            fl::HandleData const h = handleFromObjKey(k, obj.current);
            FakeWorldObject const& otherFwo = woState(h);
            if (hostile == 1 && !otherFwo.hostileToAll)
                continue;
            if (hostile == 2 && !otherFwo.friendlyToAll)
                continue;
            if (objKind == fl::ObjKind::Player || objKind == fl::ObjKind::Creature)
            {
                if (dead == 1 && !obj.info.alive)
                    continue;
                if (dead == 2 && obj.info.alive)
                    continue;
            }
            double const dx = obj.info.x - me.x, dy = obj.info.y - me.y, dz = obj.info.z - me.z;
            double const d = std::max(0.0, std::sqrt(dx * dx + dy * dy + dz * dz) - fwo.combatReach - otherFwo.combatReach);
            if (d <= range)
                found.emplace_back(d, h);
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        if (op == "worldobject.getNearObject")
        {
            if (!found.empty())
                out.emplace_back(found.front().second);
        }
        else
        {
            for (auto const& item : found)
                out.emplace_back(item.second);
        }
    }
    else if (op == "worldobject.playDirectSound" || op == "worldobject.playDistanceSound" ||
             op == "worldobject.playMusic")
    {
        fl::HandleData target;
        uint64_t const targetLow = handleAt(args, 1, target) ? target.guid.lo : 0;
        fwo.sounds.push_back(FakeWorldObject::SoundCall{op, static_cast<uint32_t>(intAt(args, 0)), targetLow});
    }
    else if (op == "worldobject.say" || op == "worldobject.yell" || op == "unit.say" || op == "unit.yell")
    {
        fwo.chats.push_back(
            FakeWorldObject::WoChat{op, stringAt(args, 0), static_cast<uint32_t>(intAt(args, 1)), 0, false});
    }
    else if (op == "worldobject.whisper" || op == "unit.whisper")
    {
        fl::HandleData receiver;
        uint64_t const targetLow = handleAt(args, 2, receiver) ? receiver.guid.lo : 0;
        fwo.chats.push_back(FakeWorldObject::WoChat{op, stringAt(args, 0), static_cast<uint32_t>(intAt(args, 1)),
                                                    targetLow, boolAt(args, 3, false)});
    }
    return true;
}

bool FakeHost::gameObjectStateCall(fl::HandleData const& self, std::string const& op,
                                   std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    static std::set<std::string> const ops = {
        "gameobject.getDisplayId",     "gameobject.getLootRecipient", "gameobject.getLootRecipientGroup",
        "gameobject.hasQuest",         "gameobject.isActive",      "gameobject.isDestructible",
        "gameobject.isTransport",      "gameobject.removeFromWorld", "gameobject.setRespawnTime",
        "gameobject.setSpawnedByDefault"};
    if (!ops.count(op))
        return false;

    FakeGameObject& g = goState(self);
    if (op == "gameobject.getDisplayId")
        out.emplace_back(static_cast<int64_t>(g.displayId));
    else if (op == "gameobject.getLootRecipient")
    {
        if (g.lootRecipient)
            out.emplace_back(*g.lootRecipient);
    }
    else if (op == "gameobject.getLootRecipientGroup")
    {
        if (g.lootRecipientGroup)
            out.emplace_back(*g.lootRecipientGroup);
    }
    else if (op == "gameobject.hasQuest")
        out.emplace_back(g.quests.count(static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "gameobject.isActive")
        out.emplace_back(g.active);
    else if (op == "gameobject.isDestructible")
        out.emplace_back(g.destructible);
    else if (op == "gameobject.isTransport")
        out.emplace_back(g.transport);
    else if (op == "gameobject.removeFromWorld")
    {
        g.actions.emplace_back(op, args);
        remove(self);
    }
    else if (op == "gameobject.setRespawnTime")
        g.respawnTime = static_cast<int32_t>(intAt(args, 0, 0));
    else if (op == "gameobject.setSpawnedByDefault")
        g.spawnedByDefault = boolAt(args, 0, true);
    return true;
}

bool FakeHost::questStateCall(fl::HandleData const& self, std::string const& op,
                              std::vector<fl::EventValue> const& args, std::vector<fl::EventValue>& out)
{
    auto it = questStates.find(static_cast<uint32_t>(self.guid.lo));
    if (it == questStates.end())
        return false;
    FakeQuest const& q = it->second;
    if (op == "quest.getFlags")
        out.emplace_back(static_cast<int64_t>(q.flags));
    else if (op == "quest.getId")
        out.emplace_back(static_cast<int64_t>(q.id));
    else if (op == "quest.getLevel")
        out.emplace_back(static_cast<int64_t>(q.level));
    else if (op == "quest.getMaxLevel")
        out.emplace_back(static_cast<int64_t>(q.maxLevel));
    else if (op == "quest.getMinLevel")
        out.emplace_back(static_cast<int64_t>(q.minLevel));
    else if (op == "quest.getNextQuestId")
        out.emplace_back(static_cast<int64_t>(q.nextQuestId));
    else if (op == "quest.getNextQuestInChain")
        out.emplace_back(static_cast<int64_t>(q.nextQuestInChain));
    else if (op == "quest.getPrevQuestId")
        out.emplace_back(static_cast<int64_t>(q.prevQuestId));
    else if (op == "quest.getTitle")
        out.emplace_back(q.title);
    else if (op == "quest.getType")
        out.emplace_back(static_cast<int64_t>(q.type));
    else if (op == "quest.hasFlag")
        out.emplace_back((q.flags & static_cast<uint32_t>(intAt(args, 0))) != 0);
    else if (op == "quest.isDaily")
        out.emplace_back(q.daily);
    else if (op == "quest.isRepeatable")
        out.emplace_back(q.repeatable);
    else
        return false;
    return true;
}

bool FakeHost::performSpawn(int type, uint32_t entry, uint32_t mapId, uint32_t instanceId, float, float, float, float,
                            bool save, uint32_t respawnOrDespawn, uint32_t phase, fl::HandleData& out)
{
    if (type != 1 && type != 2)
        return false;
    // The same rule as the module: this map's thread or the idle world thread spawns now, anyone else queues it.
    if (rules.placeForMap(flh::MapKey{mapId, instanceId}) == flh::AccessRules::Place::Defer)
    {
        deferred.push([this, type, entry, mapId, instanceId, save, respawnOrDespawn, phase]
                      { spawns.push_back(SpawnRequest{type, entry, mapId, instanceId, save, respawnOrDespawn, phase}); });
        return false;
    }
    spawns.push_back(SpawnRequest{type, entry, mapId, instanceId, save, respawnOrDespawn, phase});
    if (!spawnResult)
        return false;
    out = *spawnResult;
    return true;
}

void FakeHost::runCommand(std::string const& line)
{
    if (rules.placeForWorld() == flh::AccessRules::Place::Direct)
        commands.push_back(line);
    else
        deferred.push([this, line] { commands.push_back(line); });
}

bool FakeHost::rawQuery(std::string const&, std::string const& sql, size_t, size_t, fl::DbRows& out,
                        std::string& error)
{
    rawQueries.push_back(sql);
    if (rawFail)
    {
        error = "table 'world.nosuch' doesn't exist";
        return false;
    }
    auto it = rawResults.find(sql);
    if (it == rawResults.end())
        return false;
    out = it->second;
    return true;
}

bool FakeHost::rawExecute(std::string const&, std::string const& sql)
{
    rawExecuted.push_back(sql);
    return true;
}

// ---- E2 globals ----
bool FakeHost::guildByName(std::string const& name, fl::HandleData& out)
{
    auto it = guildsByName.find(name);
    if (it == guildsByName.end())
        return false;
    out = it->second;
    return true;
}

bool FakeHost::guildByLeaderGuid(fl::Guid const& leader, fl::HandleData& out)
{
    auto it = guildsByLeaderLow.find(leader.lo);
    if (it == guildsByLeaderLow.end())
        return false;
    out = it->second;
    return true;
}

int FakeHost::banPlayer(int mode, std::string const& target, uint32_t duration, std::string const& reason,
                        std::string const& who)
{
    bans.push_back(BanRequest{mode, target, duration, reason, who});
    return banResult;
}

std::vector<uint32_t> FakeHost::sendMail(std::string const& subject, std::string const& text, uint32_t receiver,
                                         uint32_t sender, uint32_t stationery, uint32_t delay, uint32_t money,
                                         uint32_t cod, std::vector<std::pair<uint32_t, uint32_t>> const& items)
{
    mails.push_back(MailRequest{subject, text, receiver, sender, stationery, delay, money, cod, items});
    return mailItemGuids;
}

void FakeHost::addVendorItem(uint32_t entry, uint32_t item, int32_t maxCount, uint32_t incrTime, uint32_t extendedCost)
{
    vendorCalls.push_back(VendorCall{0, entry, item, maxCount, incrTime, extendedCost});
}

void FakeHost::vendorRemoveItem(uint32_t entry, uint32_t item)
{
    vendorCalls.push_back(VendorCall{1, entry, item, 0, 0, 0});
}

void FakeHost::vendorRemoveAllItems(uint32_t entry)
{
    vendorCalls.push_back(VendorCall{2, entry, 0, 0, 0, 0});
}

bool FakeHost::isGameEventActive(uint32_t id)
{
    return std::find(activeEvents.begin(), activeEvents.end(), id) != activeEvents.end();
}

// ---- E3: asynchronous SQL ----
bool FakeHost::rawQueryAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes,
                             uint64_t id, std::string& error)
{
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    if (failAsync)
    {
        error = "queue full";
        return false;
    }
    asyncRequests.push_back(AsyncRequest{id, db, sql, maxRows, maxBytes});
    return true;
}

void FakeHost::cancelRawQuery(uint64_t id)
{
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    asyncCancelled.push_back(id);
    for (auto it = asyncReady.begin(); it != asyncReady.end();)
        it = it->first == id ? asyncReady.erase(it) : it + 1;
    asyncReadyErrors.erase(id);
}

bool FakeHost::takeRawQueryResult(uint64_t& id, fl::DbRows& out, std::string& error)
{
    notePoll("takeRawQueryResult");
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    if (!asyncReady.empty())
    {
        id = asyncReady.front().first;
        out = std::move(asyncReady.front().second);
        asyncReady.pop_front();
        return true;
    }
    if (!asyncReadyErrors.empty())
    {
        auto it = asyncReadyErrors.begin();
        id = it->first;
        error = it->second;
        asyncReadyErrors.erase(it);
        return true;
    }
    return false;
}

// ---- E3: ForeverData ----
bool FakeHost::dataLoad(std::vector<fl::DataRow>& out, std::string& error)
{
    notePoll("dataLoad");
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    ++dataLoadCalls;
    if (!dataLoadOk)
    {
        error = dataLoadError;
        return false;
    }
    out = dataRows;
    return true;
}

bool FakeHost::dataWrite(fl::DataWrite const& w)
{
    notePoll("dataWrite");
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    if (dataQueueFull || (dataQueueLimit && dataPending.size() >= dataQueueLimit))
        return false;
    dataWriteOrder.push_back(w.script + "/" + w.key);
    dataPending.push_back(w);
    if (!dataHold)
        finishDataWrites();
    return true;
}

void FakeHost::finishDataWrites()
{
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    for (auto& w : dataPending)
    {
        fl::DataWriteResult r;
        r.token = w.token;
        if (dataStoreFailures > 0)
        {
            --dataStoreFailures;
            r.ok = false;
            r.error = "write refused";
        }
        else
        {
            r.ok = true;
            if (w.erase)
            {
                ++dataEraseCalls;
                storedData.erase({w.script, w.key});
                erasedData.insert({w.script, w.key});
            }
            else
            {
                ++dataStoreCalls;
                storedData[{w.script, w.key}] = w.value;
            }
        }
        dataDone.push_back(std::move(r));
    }
    dataPending.clear();
}

void FakeHost::takeDataWrites(std::vector<fl::DataWriteResult>& out)
{
    notePoll("takeDataWrites");
    std::lock_guard<std::recursive_mutex> dbLock(_dbMutex);
    if (dataCompleteOnPoll)
        finishDataWrites();
    for (auto& r : dataDone)
        out.push_back(std::move(r));
    dataDone.clear();
}

bool FakeHost::runGmLine(std::string const& line, std::vector<std::string>& out)
{
    gmLines.push_back(line);
    if (onGm)
        onGm();
    out.push_back("ran: " + line);
    return true;
}

TempDir::TempDir()
{
    static std::atomic<int> counter{0};
    _path = std::filesystem::temp_directory_path() /
            ("forever-lua-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(_path);
}

TempDir::~TempDir()
{
    std::error_code ec;
    std::filesystem::remove_all(_path, ec);
}

void TempDir::write(std::string const& relName, std::string const& content)
{
    auto p = _path / relName;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

void TempDir::remove(std::string const& relName)
{
    std::filesystem::remove(_path / relName);
}

fl::Config testConfig(std::string const& scriptPath)
{
    fl::Config c;
    c.scriptPath = scriptPath;
    c.commandRoots = {"tt"};
    // Generous budgets: the VM that runs the suite is shared and sanitizer builds are slow. The tests that are about
    // the budget set their own tight ones.
    c.eventBudget = {5000, 500'000'000};
    c.commandBudget = {10000, 1'000'000'000};
    c.loadBudget = {10000, 1'000'000'000};
    return c;
}

std::shared_ptr<fl::SchemaSnapshot> standardSchema()
{
    auto s = std::make_shared<fl::SchemaSnapshot>();
    s->addTable("creature_template", {"entry", "name", "minlevel", "maxlevel", "faction"});
    s->addTable("creature", {"guid", "id", "map", "position_x", "position_y", "position_z"});
    return s;
}
}  // namespace fltest
