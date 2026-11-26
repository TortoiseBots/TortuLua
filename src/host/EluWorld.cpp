// The Eluna mode answers of ForeverHost (E1): the class methods of Object/WorldObject/Unit/Player/Creature/GameObject,
// the Player gossip calls, the global lookups (players, spawn, world message, item link, area name, map/quest), raw SQL
// through the module's own connections, and the world-vs-map access rule.
//
// Every op validates its handles through flh::find* (the handle is re-resolved; nothing here keeps a pointer).
#include "ForeverHost.h"

#include "Battleground.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "DataSql.h"
#include "GuildWrite.h"
#include "NearSearch.h"
#include "WorldCall.h"

#include "Chat.h"
#include "ChatPackets.h"
#include "ChatCommand.h"
#include "ChatTextBuilder.h"
#include "Corpse.h"
#include "Creature.h"
#include "Pet.h"
#include "CreatureAI.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameAccess.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "Group.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GroupMgr.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceLockMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "LootItemType.h"
#include "LuaDb.h"
#include "Mail.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NPCPackets.h"
#include "PartyPackets.h"
#include "Waypoints/WaypointManager.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "DB2Structure.h"
#include "QuestDef.h"
#include "RealmList.h"
#include "RestMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "TradeData.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Weather.h"
#include "Unit.h"
#include "World.h"

#include "engine/Engine.h"
#include "engine/GuidText.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

#if __has_include("revision_data.h")
#include "revision_data.h"
#endif

namespace flh
{
namespace
{
using fl::EventValue;
using fl::HandleData;
using fl::ObjKind;

Unit* unitOf(HandleData const& h) { return findUnit(h); }
Player* playerOf(HandleData const& h) { return findPlayer(h); }
Creature* creatureOf(HandleData const& h) { return findCreature(h); }
GameObject* goOf(HandleData const& h) { return findGameObject(h); }
WorldObject* woOf(HandleData const& h) { return findObject(h); }

bool asInt(EventValue const& v, int64_t& out)
{
    if (auto const* i = std::get_if<int64_t>(&v.v))
    {
        out = *i;
        return true;
    }
    if (auto const* d = std::get_if<double>(&v.v))
    {
        out = static_cast<int64_t>(*d);
        return true;
    }
    return false;
}

bool asDouble(EventValue const& v, double& out)
{
    if (auto const* d = std::get_if<double>(&v.v))
    {
        out = *d;
        return true;
    }
    if (auto const* i = std::get_if<int64_t>(&v.v))
    {
        out = static_cast<double>(*i);
        return true;
    }
    return false;
}

bool asBool(EventValue const& v, bool& out)
{
    if (auto const* b = std::get_if<bool>(&v.v))
    {
        out = *b;
        return true;
    }
    return false;
}

bool asString(EventValue const& v, std::string& out)
{
    if (auto const* s = std::get_if<std::string>(&v.v))
    {
        out = *s;
        return true;
    }
    return false;
}

bool asHandle(EventValue const& v, HandleData& out)
{
    if (auto const* h = std::get_if<HandleData>(&v.v))
    {
        out = *h;
        return true;
    }
    return false;
}

int64_t iArg(std::vector<EventValue> const& a, size_t i, int64_t def = 0)
{
    int64_t v = def;
    return i < a.size() && asInt(a[i], v) ? v : def;
}

double dArg(std::vector<EventValue> const& a, size_t i, double def = 0)
{
    double v = def;
    return i < a.size() && asDouble(a[i], v) ? v : def;
}

bool bArg(std::vector<EventValue> const& a, size_t i, bool def = false)
{
    bool v = def;
    return i < a.size() && asBool(a[i], v) ? v : def;
}

std::string sArg(std::vector<EventValue> const& a, size_t i)
{
    std::string v;
    asString(a.size() > i ? a[i] : EventValue(), v);
    return v;
}

// A LocaleConstant argument: the docs' 0..8, plus the core's ptBR/itIT; anything else falls back to enUS (the core indexes
// its name table with it, so an unchecked value reads past the table).
LocaleConstant localeArgAt(std::vector<EventValue> const& a, size_t i)
{
    int64_t const v = iArg(a, i, 0);
    return v >= 0 && v < TOTAL_LOCALES && v != LOCALE_none ? static_cast<LocaleConstant>(v) : LOCALE_enUS;
}

// A guid given as the 32-digit Object:GetGUID() text: the 128-bit value, empty when absent. A legacy integer (high half 0,
// low half set) means a guid of the one kind the argument takes: an item, or a player.
// A zero-high guid is a typeless (HighGuid::Null) value that no core object ever carries, so it would match nothing.
enum class LegacyGuid { Player, Item, Creature, Pet };
ObjectGuid guidArgAt(std::vector<EventValue> const& a, size_t i, LegacyGuid legacy)
{
    fl::Guid g;
    if (i < a.size() && fl::parseGuidText(sArg(a, i), g))
    {
        if (g.hi == 0 && g.lo != 0)
        {
            auto const low = static_cast<ObjectGuid::LowType>(g.lo);
            switch (legacy)
            {
                case LegacyGuid::Item: return ObjectGuid::Create<HighGuid::Item>(low);
                case LegacyGuid::Creature: return ObjectGuid::Create<HighGuid::Creature>(0, 0, low);
                case LegacyGuid::Pet: return ObjectGuid::Create<HighGuid::Pet>(0, 0, low);
                case LegacyGuid::Player: return ObjectGuid::Create<HighGuid::Player>(low);
            }
        }
        return toGuid(g);
    }
    return ObjectGuid::Empty;
}

// The HighGuid value of a handle's guid (GetGUIDType).
int typeOfGuid(fl::Guid const& g)
{
    ObjectGuid guid = toGuid(g);
    return static_cast<int>(guid.GetHigh());
}

void pushHandle(std::vector<EventValue>& out, fl::HandleData const& h) { out.emplace_back(EventValue(h)); }

void lootRecipientFromTapList(WorldObject const* obj, GuidUnorderedSet const& tapList, bool wantGroup,
                              std::vector<EventValue>& out)
{
    Player* recipient = nullptr;
    for (ObjectGuid const& guid : tapList)
        if (Player* player = ObjectAccessor::GetPlayer(*obj, guid))
            if (player->IsInWorld() && (!recipient || player->GetGUID() < recipient->GetGUID()))
                recipient = player;
    if (!recipient)
        return;
    if (!wantGroup)
    {
        pushHandle(out, handleFor(recipient));
        return;
    }
    if (Group* group = recipient->GetGroup())
    {
        HandleData h;
        h.kind = ObjKind::Group;
        h.guid = fromGuid(group->GetGUID());
        pushHandle(out, h);
    }
}

// The Spell and Aura handles of the running Lua call (E2): the docs' objects are transient, so the module only keeps
// them while the call that produced them runs (Host::endLuaCall clears the store). The handle carries the caster guid,
// the spell id (mapId) and a store id (instanceId).
struct TransientObjects
{
    std::mutex mutex;
    uint32_t nextId = 1;
    std::map<uint32_t, Spell*> spells;
    std::map<uint32_t, Aura*> auras;
    std::map<uint32_t, AuraEffect*> auraEffects;
    std::map<uint32_t, ProcEventInfo*> procInfos;
};
TransientObjects& transient()
{
    static TransientObjects store;
    return store;
}

Spell* spellById(uint32_t id)
{
    TransientObjects& t = transient();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.spells.find(id);
    return it == t.spells.end() ? nullptr : it->second;
}
Aura* auraById(uint32_t id)
{
    TransientObjects& t = transient();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.auras.find(id);
    return it == t.auras.end() ? nullptr : it->second;
}
AuraEffect* auraEffectById(uint32_t id)
{
    TransientObjects& t = transient();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.auraEffects.find(id);
    return it == t.auraEffects.end() ? nullptr : it->second;
}
ProcEventInfo* procInfoById(uint32_t id)
{
    TransientObjects& t = transient();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.procInfos.find(id);
    return it == t.procInfos.end() ? nullptr : it->second;
}
uint32_t rememberAura(Aura* aura)
{
    TransientObjects& t = transient();
    std::lock_guard<std::mutex> lock(t.mutex);
    uint32_t const id = t.nextId++;
    t.auras[id] = aura;
    return id;
}

fl::HandleData objectHandle(WorldObject const* o)
{
    if (!o || !o->IsInWorld())
        return fl::HandleData{};
    return handleFor(o);
}

// The handle of a live world object, or nothing: a read answers nil for an object that left the world (a guid-0 handle
// would name the wrong object).
std::optional<fl::HandleData> liveHandle(WorldObject const* o)
{
    if (!o || !o->IsInWorld())
        return std::nullopt;
    return handleFor(o);
}

// The handle of a spell target of any documented kind (a unit, a game object, a corpse; items are handled by the caller).
std::optional<fl::HandleData> targetHandle(WorldObject* o)
{
    if (!o || !o->IsInWorld())
        return std::nullopt;
    if (o->IsUnit())
        return handleFor(o);
    if (o->IsGameObject())
    {
        fl::HandleData h = handleFor(o);
        h.kind = ObjKind::GameObject;   // handleFor(WorldObject*) reads an unknown world object as a game object
        return h;
    }
    if (Corpse* corpse = o->ToCorpse())
        return handleForCorpse(corpse);
    return std::nullopt;
}

// A documented spell entry by id. Core spell data lives for the whole server run, so a SpellInfo handle never goes stale.
SpellInfo const* spellEntry(uint32_t id) { return sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE); }

// The effect at a script's effect index. The core's effect list is as long as the spell's highest used effect index
// (SpellInfo::GetEffect ASSERTs the index and is undefined past it in a release build), so the index is checked first and
// the script gets an ordinary Lua error (fl::safe turns this exception into one).
SpellEffectInfo const& effectAt(SpellInfo const* info, uint32_t index)
{
    if (index >= info->GetEffects().size())
        throw std::out_of_range("effect index " + std::to_string(index) + " is not an effect of spell " +
                                std::to_string(info->Id));
    return info->GetEffect(static_cast<SpellEffIndex>(index));
}

// The mana entry of a spell's power costs (the first entry with PowerType POWER_MANA), or null.
SpellPowerEntry const* manaCostOf(SpellInfo const* info)
{
    for (SpellPowerEntry const* cost : info->PowerCosts)
        if (cost && cost->PowerType == POWER_MANA)
            return cost;
    return nullptr;
}

// The SpellInfo ops that answer one whole number: op -> reader (docs/SpellInfo/index.html, one line each).
struct SpellInfoNumOp
{
    char const* op;
    int64_t (*get)(SpellInfo const*);
};
SpellInfoNumOp const kSpellInfoNumOps[] = {
    {"spellinfo.getId", [](SpellInfo const* s) { return static_cast<int64_t>(s->Id); }},
    {"spellinfo.getCategory", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetCategory()); }},
    {"spellinfo.getDispel", [](SpellInfo const* s) { return static_cast<int64_t>(s->Dispel); }},
    {"spellinfo.getMechanic", [](SpellInfo const* s) { return static_cast<int64_t>(s->Mechanic); }},
    {"spellinfo.getSchoolMask", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetSchoolMask()); }},
    {"spellinfo.getAttributes", [](SpellInfo const* s) { return static_cast<int64_t>(s->Attributes); }},
    {"spellinfo.getAttributesCu", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesCu); }},
    {"spellinfo.getAttributesEx", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx); }},
    {"spellinfo.getAttributesEx2", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx2); }},
    {"spellinfo.getAttributesEx3", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx3); }},
    {"spellinfo.getAttributesEx4", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx4); }},
    {"spellinfo.getAttributesEx5", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx5); }},
    {"spellinfo.getAttributesEx6", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx6); }},
    {"spellinfo.getAttributesEx7", [](SpellInfo const* s) { return static_cast<int64_t>(s->AttributesEx7); }},
    {"spellinfo.getDuration", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetDuration()); }},
    {"spellinfo.getMaxDuration", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetMaxDuration()); }},
    {"spellinfo.getRecoveryTime", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetRecoveryTime()); }},
    {"spellinfo.getCategoryRecoveryTime",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->CategoryRecoveryTime); }},
    {"spellinfo.getStackAmount", [](SpellInfo const* s) { return static_cast<int64_t>(s->StackAmount); }},
    {"spellinfo.getMaxAffectedTargets", [](SpellInfo const* s) { return static_cast<int64_t>(s->MaxAffectedTargets); }},
    {"spellinfo.getSpellFamilyName", [](SpellInfo const* s) { return static_cast<int64_t>(s->SpellFamilyName); }},
    {"spellinfo.getExplicitTargetMask",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->GetExplicitTargetMask()); }},
    {"spellinfo.getAuraState", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetAuraState()); }},
    {"spellinfo.getSpellSpecific", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetSpellSpecific()); }},
    {"spellinfo.getBaseLevel", [](SpellInfo const* s) { return static_cast<int64_t>(s->BaseLevel); }},
    {"spellinfo.getSpellLevel", [](SpellInfo const* s) { return static_cast<int64_t>(s->SpellLevel); }},
    {"spellinfo.getMaxLevel", [](SpellInfo const* s) { return static_cast<int64_t>(s->MaxLevel); }},
    {"spellinfo.getMaxTargetLevel", [](SpellInfo const* s) { return static_cast<int64_t>(s->MaxTargetLevel); }},
    {"spellinfo.getDmgClass", [](SpellInfo const* s) { return static_cast<int64_t>(s->DmgClass); }},
    {"spellinfo.getProcChance", [](SpellInfo const* s) { return static_cast<int64_t>(s->ProcChance); }},
    {"spellinfo.getProcCharges", [](SpellInfo const* s) { return static_cast<int64_t>(s->ProcCharges); }},
    {"spellinfo.getRank", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetRank()); }},
    {"spellinfo.getTargets", [](SpellInfo const* s) { return static_cast<int64_t>(s->Targets); }},
    {"spellinfo.getTargetCreatureType", [](SpellInfo const* s) { return static_cast<int64_t>(s->TargetCreatureType); }},
    {"spellinfo.getInterruptFlags",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->InterruptFlags.AsUnderlyingType()); }},
    {"spellinfo.getAuraInterruptFlags",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->AuraInterruptFlags.AsUnderlyingType()); }},
    {"spellinfo.getChannelInterruptFlags",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->ChannelInterruptFlags.AsUnderlyingType()); }},
    {"spellinfo.getStartRecoveryCategory",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->StartRecoveryCategory); }},
    {"spellinfo.getStartRecoveryTime", [](SpellInfo const* s) { return static_cast<int64_t>(s->StartRecoveryTime); }},
    {"spellinfo.getPreventionType", [](SpellInfo const* s) { return static_cast<int64_t>(s->PreventionType); }},
    {"spellinfo.getAttackType", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetAttackType()); }},
    {"spellinfo.getActiveIconID", [](SpellInfo const* s) { return static_cast<int64_t>(s->ActiveIconFileDataId); }},
    {"spellinfo.getSpellIconID", [](SpellInfo const* s) { return static_cast<int64_t>(s->IconFileDataId); }},
    {"spellinfo.getRequiresSpellFocus", [](SpellInfo const* s) { return static_cast<int64_t>(s->RequiresSpellFocus); }},
    {"spellinfo.getAreaGroupId", [](SpellInfo const* s) { return static_cast<int64_t>(s->RequiredAreasID); }},
    {"spellinfo.getCasterAuraSpell", [](SpellInfo const* s) { return static_cast<int64_t>(s->CasterAuraSpell); }},
    {"spellinfo.getCasterAuraState", [](SpellInfo const* s) { return static_cast<int64_t>(s->CasterAuraState); }},
    {"spellinfo.getCasterAuraStateNot",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->ExcludeCasterAuraState); }},
    {"spellinfo.getTargetAuraSpell", [](SpellInfo const* s) { return static_cast<int64_t>(s->TargetAuraSpell); }},
    {"spellinfo.getTargetAuraState", [](SpellInfo const* s) { return static_cast<int64_t>(s->TargetAuraState); }},
    {"spellinfo.getTargetAuraStateNot",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->ExcludeTargetAuraState); }},
    {"spellinfo.getFacingCasterFlags", [](SpellInfo const* s) { return static_cast<int64_t>(s->FacingCasterFlags); }},
    {"spellinfo.getEquippedItemClass", [](SpellInfo const* s) { return static_cast<int64_t>(s->EquippedItemClass); }},
    {"spellinfo.getEquippedItemSubClassMask",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->EquippedItemSubClassMask); }},
    {"spellinfo.getEquippedItemInventoryTypeMask",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->EquippedItemInventoryTypeMask); }},
    // The docs type the stance masks as uint32; the core keeps them 64-bit, so a script sees the low 32 bits it asked for.
    {"spellinfo.getStances", [](SpellInfo const* s) { return static_cast<int64_t>(static_cast<uint32>(s->Stances)); }},
    {"spellinfo.getStancesNot",
     [](SpellInfo const* s) { return static_cast<int64_t>(static_cast<uint32>(s->StancesNot)); }},
    // ProcFlagsInit holds the classic ProcFlags in its first 32-bit part (docs: one number).
    {"spellinfo.getProcFlags", [](SpellInfo const* s) { return static_cast<int64_t>(static_cast<uint32>(s->ProcFlags[0])); }},
    {"spellinfo.calcCastTime", [](SpellInfo const* s) { return static_cast<int64_t>(s->CalcCastTime()); }},
    {"spellinfo.getAllEffectsMechanicMask", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetAllEffectsMechanicMask()); }},
    {"spellinfo.getAllowedMechanicMask", [](SpellInfo const* s) { return static_cast<int64_t>(s->GetAllowedMechanicMask()); }},
    {"spellinfo.getExcludeCasterAuraSpell", [](SpellInfo const* s) { return static_cast<int64_t>(s->ExcludeCasterAuraSpell); }},
    {"spellinfo.getExcludeTargetAuraSpell", [](SpellInfo const* s) { return static_cast<int64_t>(s->ExcludeTargetAuraSpell); }},
    {"spellinfo.getDiminishingReturnsGroupForSpell",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->GetDiminishingReturnsGroupForSpell()); }},
    {"spellinfo.getDiminishingReturnsGroupType",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->GetDiminishingReturnsGroupType()); }},
    {"spellinfo.getDiminishingReturnsLimitDuration",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->GetDiminishingReturnsLimitDuration()); }},
    {"spellinfo.getDiminishingReturnsMaxLevel",
     [](SpellInfo const* s) { return static_cast<int64_t>(s->GetDiminishingReturnsMaxLevel()); }},
};

// The SpellInfo ops that answer a decimal number.
struct SpellInfoFloatOp
{
    char const* op;
    double (*get)(SpellInfo const*);
};
SpellInfoFloatOp const kSpellInfoFloatOps[] = {
    {"spellinfo.getSpeed", [](SpellInfo const* s) { return static_cast<double>(s->Speed); }},
    {"spellinfo.getMaxRange", [](SpellInfo const* s) { return static_cast<double>(s->GetMaxRange()); }},
    {"spellinfo.getMinRange", [](SpellInfo const* s) { return static_cast<double>(s->GetMinRange()); }},
};

// The SpellInfo ops that answer true or false and take no argument.
struct SpellInfoBoolOp
{
    char const* op;
    bool (*get)(SpellInfo const*);
};
SpellInfoBoolOp const kSpellInfoBoolOps[] = {
    {"spellinfo.hasAreaAuraEffect", [](SpellInfo const* s) { return s->HasAreaAuraEffect(); }},
    {"spellinfo.hasOnlyDamageEffects", [](SpellInfo const* s) { return s->HasOnlyDamageEffects(); }},
    {"spellinfo.hasInitialAggro", [](SpellInfo const* s) { return s->HasInitialAggro(); }},
    {"spellinfo.isPassive", [](SpellInfo const* s) { return s->IsPassive(); }},
    {"spellinfo.isAutocastable", [](SpellInfo const* s) { return s->IsAutocastable(); }},
    {"spellinfo.isStackableWithRanks", [](SpellInfo const* s) { return s->IsStackableWithRanks(); }},
    {"spellinfo.isChanneled", [](SpellInfo const* s) { return s->IsChanneled(); }},
    {"spellinfo.isPositive", [](SpellInfo const* s) { return s->IsPositive(); }},
    {"spellinfo.isAffectingArea", [](SpellInfo const* s) { return s->IsAffectingArea(); }},
    {"spellinfo.isTargetingArea", [](SpellInfo const* s) { return s->IsTargetingArea(); }},
    {"spellinfo.needsExplicitUnitTarget", [](SpellInfo const* s) { return s->NeedsExplicitUnitTarget(); }},
    {"spellinfo.isDeathPersistent", [](SpellInfo const* s) { return s->IsDeathPersistent(); }},
    {"spellinfo.isRequiringDeadTarget", [](SpellInfo const* s) { return s->IsRequiringDeadTarget(); }},
    {"spellinfo.isAllowingDeadTarget", [](SpellInfo const* s) { return s->IsAllowingDeadTarget(); }},
    {"spellinfo.isCooldownStartedOnEvent", [](SpellInfo const* s) { return s->IsCooldownStartedOnEvent(); }},
    {"spellinfo.isRanked", [](SpellInfo const* s) { return s->IsRanked(); }},
    {"spellinfo.isSingleTarget", [](SpellInfo const* s) { return s->IsSingleTarget(); }},
    {"spellinfo.isMultiSlotAura", [](SpellInfo const* s) { return s->IsMultiSlotAura(); }},
    {"spellinfo.isGroupBuff", [](SpellInfo const* s) { return s->IsGroupBuff(); }},
    {"spellinfo.isMoveAllowedChannel", [](SpellInfo const* s) { return s->IsMoveAllowedChannel(); }},
    {"spellinfo.isNextMeleeSwingSpell", [](SpellInfo const* s) { return s->IsNextMeleeSwingSpell(); }},
    {"spellinfo.isRangedWeaponSpell", [](SpellInfo const* s) { return s->IsRangedWeaponSpell(); }},
    {"spellinfo.isAutoRepeatRangedSpell", [](SpellInfo const* s) { return s->IsAutoRepeatRangedSpell(); }},
    {"spellinfo.isAffectedBySpellMods", [](SpellInfo const* s) { return s->IsAffectedBySpellMods(); }},
    {"spellinfo.isBreakingStealth",
     [](SpellInfo const* s) { return !s->HasAttribute(SPELL_ATTR0_CU_DONT_BREAK_STEALTH) && !s->HasAttribute(SPELL_ATTR1_ALLOW_WHILE_STEALTHED); }},
    {"spellinfo.isExplicitDiscovery", [](SpellInfo const* s) { return s->IsExplicitDiscovery(); }},
    {"spellinfo.isLootCrafting", [](SpellInfo const* s) { return s->IsLootCrafting(); }},
    {"spellinfo.isPassiveStackableWithRanks", [](SpellInfo const* s) { return s->IsPassiveStackableWithRanks(); }},
    {"spellinfo.isPrimaryProfession", [](SpellInfo const* s) { return s->IsPrimaryProfession(); }},
    {"spellinfo.isPrimaryProfessionFirstRank", [](SpellInfo const* s) { return s->IsPrimaryProfessionFirstRank(); }},
    {"spellinfo.isProfession", [](SpellInfo const* s) { return s->IsProfession(); }},
    {"spellinfo.isStackableOnOneSlotWithDifferentCasters",
     [](SpellInfo const* s) { return s->IsStackableOnOneSlotWithDifferentCasters(); }},
    {"spellinfo.isSelfCast",
     [](SpellInfo const* s) {
         bool hasEffect = false;
         for (SpellEffectInfo const& eff : s->GetEffects())
         {
             if (!eff.IsEffect())
                 continue;
             hasEffect = true;
             if (eff.TargetA.GetTarget() != TARGET_UNIT_CASTER ||
                 (eff.TargetB.GetTarget() != 0 && eff.TargetB.GetTarget() != TARGET_UNIT_CASTER))
                 return false;
         }
         return hasEffect;
     }},
};
}  // namespace

HandleData handleForSpell(Spell* spell)
{
    if (!spell)
        return fl::HandleData{};
    TransientObjects& t = transient();
    uint32_t id = 0;
    {
        std::lock_guard<std::mutex> lock(t.mutex);
        id = t.nextId++;
        t.spells[id] = spell;
    }
    fl::HandleData h;
    h.kind = fl::ObjKind::Spell;
    h.guid = spell->GetCaster() ? fromGuid(spell->GetCaster()->GetGUID()) : fl::Guid{};
    h.mapId = spell->GetSpellInfo() ? spell->GetSpellInfo()->Id : 0;
    h.instanceId = id;
    return h;
}

HandleData handleForAuraEffect(AuraEffect* eff)
{
    if (!eff)
        return fl::HandleData{};
    TransientObjects& t = transient();
    uint32_t id = 0;
    {
        std::lock_guard<std::mutex> lock(t.mutex);
        id = t.nextId++;
        t.auraEffects[id] = eff;
    }
    fl::HandleData h;
    h.kind = fl::ObjKind::AuraEffect;
    h.guid = eff->GetCaster() ? fromGuid(eff->GetCaster()->GetGUID()) : fl::Guid{};
    h.mapId = eff->GetId();
    h.instanceId = id;
    return h;
}

HandleData handleForProcInfo(ProcEventInfo* proc)
{
    if (!proc)
        return fl::HandleData{};
    TransientObjects& t = transient();
    uint32_t id = 0;
    {
        std::lock_guard<std::mutex> lock(t.mutex);
        id = t.nextId++;
        t.procInfos[id] = proc;
    }
    fl::HandleData h;
    h.kind = fl::ObjKind::ElunaProcInfo;
    h.guid = proc->GetActor() ? fromGuid(proc->GetActor()->GetGUID()) : fl::Guid{};
    h.mapId = proc->GetSpellInfo() ? proc->GetSpellInfo()->Id : 0;
    h.instanceId = id;
    return h;
}

void ForeverHost::endLuaCall()
{
    TransientObjects& t = transient();
    std::lock_guard<std::mutex> lock(t.mutex);
    t.spells.clear();
    t.auras.clear();
    t.auraEffects.clear();
    t.procInfos.clear();
}

fl::Access ForeverHost::checkAccess(fl::HandleData const& h) { return accessFor(h); }

bool ForeverHost::currentMap(fl::HandleData const& h, uint32_t& mapId, uint32_t& instanceId)
{
    return currentMapOf(h, mapId, instanceId);
}

bool ForeverHost::objectInWorld(fl::HandleData const& h)
{
    switch (h.kind)
    {
        case fl::ObjKind::Creature:
        case fl::ObjKind::GameObject:
        case fl::ObjKind::Corpse:
        case fl::ObjKind::Player: return findObject(h) != nullptr;   // a fresh GUID lookup, null when out of the world
        default: return true;
    }
}

bool ForeverHost::gameCall(fl::HandleData const& self, std::string_view op, std::vector<EventValue> const& args,
                           std::vector<EventValue>& out)
{
    std::string const name(op);
    out.clear();

    // ---- Map ----
    if (name == "map.getName")
    {
        if (Map* m = findMap(self.mapId, self.instanceId))
            out.emplace_back(m->GetMapName());
        return true;
    }

    // ---- Map (E5 long tail) ----
    if (self.kind == ObjKind::Map)
    {
        Map* m = findMap(self.mapId, self.instanceId);
        if (!m)
            return false;
        if (name == "map.getPlayerCount" || name == "map.getPlayers")
        {
            size_t count = 0;
            for (MapReference const& ref : m->GetPlayers())
            {
                Player* p = ref.GetSource();
                if (!p || !p->IsInWorld() || p->IsGameMaster())
                    continue;
                ++count;
                if (name == "map.getPlayers")
                    pushHandle(out, handleFor(p));
            }
            if (name == "map.getPlayerCount")
                out.emplace_back(static_cast<int64_t>(count));
            return true;
        }
        if (name == "map.getWorldObject")
        {
            fl::Guid g;
            if (!args.empty() && fl::parseGuidText(sArg(args, 0), g) && g.hi != 0)
            {
                ObjectGuid const guid = toGuid(g);
                switch (guid.GetHigh())
                {
                    case HighGuid::Player:
                        if (Player* p = ObjectAccessor::FindPlayer(guid))
                            if (p->IsInWorld() && p->GetMap() == m)
                                pushHandle(out, handleFor(p));
                        break;
                    case HighGuid::Creature:
                        if (Creature* c = m->GetCreature(guid))
                            if (c->IsInWorld())
                                pushHandle(out, handleFor(c));
                        break;
                    case HighGuid::Pet:
                        if (Pet* pet = m->GetPet(guid))   // a pet is in the map's Pet store, not the Creature store
                            if (pet->IsInWorld())
                                pushHandle(out, handleFor(pet));
                        break;
                    case HighGuid::GameObject:
                        if (GameObject* go = m->GetGameObject(guid))
                            if (go->IsInWorld())
                                pushHandle(out, handleFor(go));
                        break;
                    case HighGuid::Corpse:
                        if (Corpse* c = m->GetCorpse(guid))
                            if (c->IsInWorld())
                                pushHandle(out, handleForCorpse(c));
                        break;
                    default: break;
                }
            }
            return true;
        }
        if (name == "map.isDungeon")
        {
            out.emplace_back(m->IsDungeon());
            return true;
        }
        if (name == "map.isRaid")
        {
            out.emplace_back(m->IsRaid());
            return true;
        }
        if (name == "map.isArena")
        {
            out.emplace_back(m->IsBattleArena());
            return true;
        }
        if (name == "map.isBattleground")
        {
            out.emplace_back(m->IsBattleground());
            return true;
        }
        if (name == "map.isEmpty")
        {
            out.emplace_back(m->GetPlayers().size() == 0);
            return true;
        }
        if (name == "map.isHeroic")
        {
            out.emplace_back(m->IsHeroic());
            return true;
        }
        if (name == "map.getDifficulty")
        {
            out.emplace_back(static_cast<int64_t>(m->GetDifficultyID()));
            return true;
        }
        if (name == "map.getAreaId")
        {
            float const x = static_cast<float>(dArg(args, 0));
            float const y = static_cast<float>(dArg(args, 1));
            float const z = static_cast<float>(dArg(args, 2));
            out.emplace_back(static_cast<int64_t>(m->GetAreaId(PhasingHandler::GetEmptyPhaseShift(), x, y, z)));
            return true;
        }
        if (name == "map.getHeight")
        {
            float const x = static_cast<float>(dArg(args, 0));
            float const y = static_cast<float>(dArg(args, 1));
            out.emplace_back(static_cast<double>(m->GetHeight(PhasingHandler::GetEmptyPhaseShift(), x, y, 0.0f)));
            return true;
        }
        if (name == "map.setWeather")
        {
            uint32 const zone = static_cast<uint32>(iArg(args, 0));
            WeatherState const state = static_cast<WeatherState>(iArg(args, 1));
            float const grade = static_cast<float>(dArg(args, 2, 0.0));
            m->SetZoneWeather(zone, state, grade);
            return true;
        }
    }

    // ---- BattleGround (E5) ----
    if (self.kind == ObjKind::BattleGround)
    {
        Map* m = findMap(self.mapId, self.instanceId);
        if (!m)
            return false;
        BattlegroundMap* bm = m->ToBattlegroundMap();
        Battleground* bg = bm ? bm->GetBG() : nullptr;
        if (!bg)
            return false;
        if (name == "battleground.getName")
        {
            out.emplace_back(bg->GetName());
            return true;
        }
        if (name == "battleground.getStatus")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetStatus()));
            return true;
        }
        if (name == "battleground.getWinner")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetWinner()));
            return true;
        }
        if (name == "battleground.getEndTime")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetRemainingTime()));
            return true;
        }
        if (name == "battleground.getBracketId")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetBracketId()));
            return true;
        }
        if (name == "battleground.getMaxLevel")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetMaxLevel()));
            return true;
        }
        if (name == "battleground.getMinLevel")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetMinLevel()));
            return true;
        }
        if (name == "battleground.getMaxPlayers")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetMaxPlayers()));
            return true;
        }
        if (name == "battleground.getMinPlayers")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetMinPlayers()));
            return true;
        }
        if (name == "battleground.getMaxPlayersPerTeam")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetMaxPlayersPerTeam()));
            return true;
        }
        if (name == "battleground.getMinPlayersPerTeam")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetMinPlayersPerTeam()));
            return true;
        }
        if (name == "battleground.getFreeSlotsForTeam")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetFreeSlotsForTeam(static_cast<Team>(iArg(args, 0)))));
            return true;
        }
        if (name == "battleground.getAlivePlayersCountByTeam")
        {
            out.emplace_back(
                static_cast<int64_t>(bg->GetAlivePlayersCountByTeam(static_cast<Team>(iArg(args, 0)))));
            return true;
        }
        if (name == "battleground.getBonusHonorFromKillCount")
        {
            out.emplace_back(static_cast<int64_t>(bg->GetBonusHonorFromKill(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        return false;
    }

    // ---- Corpse (E5) ----
    if (self.kind == ObjKind::Corpse)
    {
        Corpse* corpse = findCorpse(self);
        if (!corpse)
            return false;   // gone: "Corpse no longer exists"
        if (name == "corpse.getGhostTime")
        {
            out.emplace_back(static_cast<int64_t>(corpse->GetGhostTime()));
            return true;
        }
        if (name == "corpse.getOwnerGuid")
        {
            out.emplace_back(static_cast<int64_t>(corpse->GetOwnerGUID().GetCounter()));
            return true;
        }
        if (name == "corpse.getType")
        {
            out.emplace_back(static_cast<int64_t>(corpse->GetType()));
            return true;
        }
        if (name == "corpse.resetGhostTime")
        {
            corpse->ResetGhostTime();
            return true;
        }
        if (name == "corpse.saveToDB")
        {
            corpse->SaveToDB();
            return true;
        }
        return false;
    }

    // ---- Item ----
    if (self.kind == ObjKind::Item)
    {
        Item* item = findItem(self);
        if (!item && name != "object.getScale")
            return false;   // gone: the binding has already refused it through requireLive
        if (name == "object.getScale")
        {
            if (item)
                out.emplace_back(static_cast<double>(item->GetObjectScale()));
            return true;
        }
        if (name == "item.getEntry")
        {
            out.emplace_back(static_cast<int64_t>(item->GetEntry()));
            return true;
        }
        if (name == "item.getItemLink")
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                return true;
            uint32 locale = static_cast<uint32>(iArg(args, 0, 0));
            if (locale >= TOTAL_LOCALES || locale == LOCALE_none)
                locale = LOCALE_enUS;   // the docs' LocaleConstant; an unknown one falls back to enUS
            out.emplace_back("|cffffffff|Hitem:" + std::to_string(proto->GetId()) + ":0:0:0:0:0:0:0|h[" +
                             proto->GetName(static_cast<LocaleConstant>(locale)) + "]|h|r");
            return true;
        }
        if (name == "item.getCount")
        {
            out.emplace_back(static_cast<int64_t>(item->GetCount()));
            return true;
        }
        if (name == "item.setCount")
        {
            item->SetCount(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "item.getOwner")
        {
            if (Player* owner = item->GetOwner())
                if (owner->IsInWorld())
                    pushHandle(out, handleFor(owner));
            return true;
        }
        if (name == "item.isEquipped")
        {
            out.emplace_back(item->IsEquipped());
            return true;
        }
        if (name == "item.getBagSlot")
        {
            out.emplace_back(static_cast<int64_t>(item->GetBagSlot()));
            return true;
        }
        if (name == "item.getSlot")
        {
            out.emplace_back(static_cast<int64_t>(item->GetSlot()));
            return true;
        }

        // ---- long tail round 1: identity and template ----
        // A getter that reads the template answers `false` when the item's template is not in the store; the binding
        // raises it as "<Method>: the item has no template on the server".
        ItemTemplate const* itemProto = item->GetTemplate();
        if (name == "item.getName" || name == "item.getItemId" || name == "item.getItemLevel" ||
            name == "item.getRequiredLevel" || name == "item.getClass" || name == "item.getSubClass" ||
            name == "item.getInventoryType" || name == "item.getQuality" || name == "item.getDisplayId" ||
            name == "item.getBuyPrice" || name == "item.getSellPrice" || name == "item.getBuyCount" ||
            name == "item.getMaxStackCount" || name == "item.getStatsCount" || name == "item.getItemSet" ||
            name == "item.getAllowableClass" || name == "item.getAllowableRace" || name == "item.getArmor" ||
            name == "item.getFlags" || name == "item.getFlags2" || name == "item.getExtraFlags" ||
            name == "item.getSpeed" || name == "item.getBagSize" ||
            name == "item.getSpellId" || name == "item.getSpellTrigger" || name == "item.getDamageInfo")
        {
            if (!itemProto)
            {
                out.emplace_back(false);
                return true;
            }
            if (name == "item.getName")
                out.emplace_back(std::string(itemProto->GetName(LOCALE_enUS)));
            else if (name == "item.getItemId")
                out.emplace_back(static_cast<int64_t>(itemProto->GetId()));
            else if (name == "item.getItemLevel")
            {
                // the owner's scaling level when the item has one (the core's own getter dereferences the owner),
                // the template's base item level otherwise
                uint32 level = itemProto->GetBaseItemLevel();
                if (Player* owner = item->GetOwner())
                    level = item->GetItemLevel(owner);
                out.emplace_back(static_cast<int64_t>(level));
            }
            else if (name == "item.getRequiredLevel")
                out.emplace_back(static_cast<int64_t>(item->GetRequiredLevel()));
            else if (name == "item.getClass")
                out.emplace_back(static_cast<int64_t>(itemProto->GetClass()));
            else if (name == "item.getSubClass")
                out.emplace_back(static_cast<int64_t>(itemProto->GetSubClass()));
            else if (name == "item.getInventoryType")
                out.emplace_back(static_cast<int64_t>(itemProto->GetInventoryType()));
            else if (name == "item.getQuality")
                out.emplace_back(static_cast<int64_t>(item->GetQuality()));
            else if (name == "item.getDisplayId")
            {
                // the base item appearance; the owner's transmog is applied only when the item has an owner
                uint32 displayId = 0;
                if (ItemModifiedAppearanceEntry const* appearance = item->GetItemModifiedAppearance())
                    if (ItemAppearanceEntry const* entry = sItemAppearanceStore.LookupEntry(appearance->ItemAppearanceID))
                        displayId = entry->ItemDisplayInfoID;
                if (!displayId)
                    if (Player* owner = item->GetOwner())
                        displayId = item->GetDisplayId(owner);
                out.emplace_back(static_cast<int64_t>(displayId));
            }
            else if (name == "item.getBuyPrice")
                out.emplace_back(static_cast<int64_t>(itemProto->GetBuyPrice()));
            else if (name == "item.getSellPrice")
                out.emplace_back(static_cast<int64_t>(itemProto->GetSellPrice()));
            else if (name == "item.getBuyCount")
                out.emplace_back(static_cast<int64_t>(itemProto->GetBuyCount()));
            else if (name == "item.getMaxStackCount")
                out.emplace_back(static_cast<int64_t>(itemProto->GetMaxStackSize()));
            else if (name == "item.getStatsCount")
            {
                // a stat slot the item does not use is -1 in the core's item data (ObjectMgr skips those)
                int64_t count = 0;
                for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
                    if (item->GetItemStatType(i) != -1)
                        ++count;
                out.emplace_back(count);
            }
            else if (name == "item.getItemSet")
                out.emplace_back(static_cast<int64_t>(itemProto->GetItemSet()));
            else if (name == "item.getAllowableClass")
                out.emplace_back(static_cast<int64_t>(itemProto->GetAllowableClass()));
            else if (name == "item.getAllowableRace")
            {
                // the two 32-bit halves of the core's race mask, low half first
                auto const races = itemProto->GetAllowableRace();
                out.emplace_back(static_cast<int64_t>((static_cast<uint64>(static_cast<uint32>(races.RawValue[1])) << 32) |
                                                      static_cast<uint32>(races.RawValue[0])));
            }
            else if (name == "item.getArmor")
                out.emplace_back(static_cast<int64_t>(itemProto->GetArmor(itemProto->GetBaseItemLevel())));
            else if (name == "item.getFlags")
                out.emplace_back(static_cast<int64_t>(static_cast<uint32>(itemProto->ExtendedData->Flags[0])));
            else if (name == "item.getFlags2")
                out.emplace_back(static_cast<int64_t>(static_cast<uint32>(itemProto->ExtendedData->Flags[1])));
            else if (name == "item.getExtraFlags")
                out.emplace_back(static_cast<int64_t>(itemProto->FlagsCu));
            else if (name == "item.getSpeed")
                out.emplace_back(static_cast<int64_t>(itemProto->GetDelay()));
            else if (name == "item.getBagSize")
                out.emplace_back(static_cast<int64_t>(item->IsBag() ? itemProto->GetContainerSlots() : 0));
            else if (name == "item.getSpellId" || name == "item.getSpellTrigger")
            {
                uint32 const index = static_cast<uint32>(iArg(args, 0));
                if (index >= itemProto->Effects.size())
                {
                    out.emplace_back(false);
                    return true;
                }
                ItemEffectEntry const* effect = itemProto->Effects[index];
                if (!effect)
                {
                    out.emplace_back(false);
                    return true;
                }
                if (name == "item.getSpellId")
                    out.emplace_back(static_cast<int64_t>(effect->SpellID));
                else
                    out.emplace_back(static_cast<int64_t>(effect->TriggerType));
            }
            else if (name == "item.getDamageInfo")
            {
                // The core's item data holds ONE damage pair (the first documented slot); the second slot has no data,
                // so it answers zeros instead of repeating the first pair (a script adding both would double it).
                float minDamage = 0.0f, maxDamage = 0.0f;
                if (iArg(args, 0) == 1)
                {
                    // the core's damage-per-second lookups are AssertEntry: an item level that has no row in one of the
                    // five damage tables would abort the server, so the rows are checked first
                    uint32 const level = itemProto->GetBaseItemLevel();
                    if (!sItemDamageAmmoStore.LookupEntry(level) || !sItemDamageOneHandStore.LookupEntry(level) ||
                        !sItemDamageOneHandCasterStore.LookupEntry(level) ||
                        !sItemDamageTwoHandStore.LookupEntry(level) ||
                        !sItemDamageTwoHandCasterStore.LookupEntry(level))
                    {
                        out.emplace_back(false);
                        return true;
                    }
                    itemProto->GetDamage(level, minDamage, maxDamage);
                }
                out.emplace_back(static_cast<int64_t>(iArg(args, 0) == 1 ? itemProto->GetDamageType() : 0));
                out.emplace_back(static_cast<double>(minDamage));
                out.emplace_back(static_cast<double>(maxDamage));
            }
            return true;
        }
        // The boolean readers answer 1 / 0 / -1 (no template): the script's bool is pushed by the binding.
        if (name == "item.isBag" || name == "item.isBoundAccountWide" || name == "item.isCurrencyToken" ||
            name == "item.isConjuredConsumable" || name == "item.isPotion" || name == "item.hasQuest" ||
            name == "item.canBeTraded" || name == "item.isNotEmptyBag")
        {
            if (!itemProto)
            {
                out.emplace_back(static_cast<int64_t>(-1));
                return true;
            }
            bool answer = false;
            if (name == "item.isBag")
                answer = item->IsBag();
            else if (name == "item.isBoundAccountWide")
                answer = item->IsBoundAccountWide();
            else if (name == "item.isCurrencyToken")
                answer = item->IsCurrencyToken();
            else if (name == "item.isConjuredConsumable")
                answer = item->IsConjuredConsumable();
            else if (name == "item.canBeTraded")
                answer = item->CanBeTraded();   // reads the template (IsBag): the guard above covers a missing one
            else if (name == "item.isNotEmptyBag")
                answer = item->IsNotEmptyBag();
            else if (name == "item.isPotion")
                answer = itemProto->GetClass() == ITEM_CLASS_CONSUMABLE &&
                         itemProto->GetSubClass() == ITEM_SUBCLASS_POTION;
            else
                answer = item->hasQuest(static_cast<uint32>(iArg(args, 0)));
            out.emplace_back(static_cast<int64_t>(answer ? 1 : 0));
            return true;
        }
        if (name == "item.getDurability")
        {
            out.emplace_back(static_cast<int64_t>(*item->m_itemData->Durability));
            return true;
        }
        if (name == "item.getMaxDurability")
        {
            out.emplace_back(static_cast<int64_t>(*item->m_itemData->MaxDurability));
            return true;
        }
        if (name == "item.getStatInfo")
        {
            uint32 const slot = static_cast<uint32>(iArg(args, 0));
            if (slot >= MAX_ITEM_PROTO_STATS)
            {
                out.emplace_back(false);
                return true;
            }
            Player* owner = item->GetOwner();
            if (!owner)
            {
                out.emplace_back(false);
                return true;
            }
            out.emplace_back(static_cast<double>(item->GetItemStatValue(slot, owner)));
            out.emplace_back(static_cast<int64_t>(item->GetItemStatType(slot)));
            return true;
        }

        // ---- state (the flags a missing template does not need) ----
        if (name == "item.isSoulBound")
        {
            out.emplace_back(item->IsSoulBound());
            return true;
        }
        if (name == "item.isBoundByEnchant")
        {
            out.emplace_back(item->IsBoundByEnchant());
            return true;
        }
        if (name == "item.isNotBoundToPlayer")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Player* player = playerOf(other))
                    out.emplace_back(item->IsBindedNotWith(player));
            return true;
        }
        if (name == "item.isLocked")
        {
            out.emplace_back(item->IsLocked());
            return true;
        }
        if (name == "item.isBroken")
        {
            out.emplace_back(item->IsBroken());
            return true;
        }
        if (name == "item.isInBag")
        {
            out.emplace_back(item->IsInBag());
            return true;
        }
        if (name == "item.isInTrade")
        {
            out.emplace_back(item->IsInTrade());
            return true;
        }
        if (name == "item.isRefundExpired")
        {
            out.emplace_back(item->IsRefundExpired());
            return true;
        }

        // ---- owner and enchants ----
        if (name == "item.getOwnerGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(item->GetOwnerGUID())));
            return true;
        }
        if (name == "item.setBinding")
        {
            // the flag alone is not saved or queued: the core marks the item changed for its owner as well
            item->SetBinding(bArg(args, 0, false));
            if (Player* owner = item->GetOwner())   // the update queue asserts on a null player
                item->SetState(ITEM_CHANGED, owner);
            return true;
        }
        if (name == "item.getEnchantmentId")
        {
            uint32 const slot = static_cast<uint32>(iArg(args, 0));
            if (slot >= MAX_ENCHANTMENT_SLOT)
            {
                out.emplace_back(false);
                return true;
            }
            out.emplace_back(static_cast<int64_t>(item->GetEnchantmentId(static_cast<EnchantmentSlot>(slot))));
            return true;
        }
        if (name == "item.setEnchantment")
        {
            uint32 const enchantId = static_cast<uint32>(iArg(args, 0));
            uint32 const slot = static_cast<uint32>(iArg(args, 1));
            if (slot >= MAX_ENCHANTMENT_SLOT)
            {
                out.emplace_back(false);
                return true;
            }
            Player* owner = item->GetOwner();
            if (!sSpellItemEnchantmentStore.LookupEntry(enchantId) || !owner)
            {
                // the binding raises: not an enchant the server knows, or no owner online (the core's own
                // SetEnchantment dereferences the owner to send the enchantment log)
                out.emplace_back(false);
                return true;
            }
            // as the core's enchant spell effects do: take the old enchant's effects off an equipped item, change the
            // slot, put the new ones on (a bare SetEnchantment would leave the old stats applied and the new unapplied)
            owner->ApplyEnchantment(item, static_cast<EnchantmentSlot>(slot), false);
            item->SetEnchantment(static_cast<EnchantmentSlot>(slot), enchantId, 0, 0);
            owner->ApplyEnchantment(item, static_cast<EnchantmentSlot>(slot), true);
            out.emplace_back(true);
            return true;
        }
        if (name == "item.clearEnchantment")
        {
            uint32 const slot = static_cast<uint32>(iArg(args, 0));
            if (slot >= MAX_ENCHANTMENT_SLOT)
            {
                out.emplace_back(false);
                return true;
            }
            Player* owner = item->GetOwner();
            if (!owner || !item->GetEnchantmentId(static_cast<EnchantmentSlot>(slot)))
            {
                out.emplace_back(false);   // nothing to remove from that slot (or no owner online to update)
                return true;
            }
            owner->ApplyEnchantment(item, static_cast<EnchantmentSlot>(slot), false);   // an equipped item's stats come off
            item->ClearEnchantment(static_cast<EnchantmentSlot>(slot));
            out.emplace_back(true);
            return true;
        }
        return false;
    }

    // ---- Group ----
    // A member change is the world thread's alone (it changes group state and sends packets for players on other maps): from a
    // map thread it is queued to the world thread's next tick and the script gets no result.
    if (self.kind == ObjKind::Group && (name == "group.addMember" || name == "group.removeMember"))
    {
        bool const add = name == "group.addMember";
        HandleData player;
        bool const hasPlayer = add && asHandle(args.empty() ? EventValue() : args[0], player);
        fl::Guid target;
        bool const hasGuid = !add && fl::parseGuidText(sArg(args, 0), target);
        int const method = add ? 0 : iArg(args, 1);
        fl::Guid const groupGuid = self.guid;
        // 0 = the group is gone, 1 = done, 2 = the core refused (or the player/guid was not usable)
        auto job = [=]() -> int
        {
            Group* g = sGroupMgr->GetGroupByGUID(toGuid(groupGuid));
            if (!g)
                return 0;
            if (add)
            {
                Player* p = hasPlayer ? findPlayer(player) : nullptr;
                return p && g->AddMember(p) ? 1 : 2;
            }
            return hasGuid && g->RemoveMember(toGuid(target), static_cast<RemoveMethod>(method)) ? 1 : 2;
        };
        WorldCallResult how = WorldCallResult::Ran;
        // The flags are the world thread's: a map thread goes through callOnWorld without reading them. A world-thread
        // call from a hook of a change (or of a core social action) already running is queued as well.
        if (rules().placeForWorld() == AccessRules::Place::Direct && socialWriteBlocked())
        {
            runSocialWrite("Group member change", [job] { (void)job(); });
            return true;   // queued: no result yet
        }
        auto guarded = [this, job]() -> int
        {
            struct Guard
            {
                bool& flag;
                ~Guard() { flag = false; }
            } reset{_socialWriteActive};
            _socialWriteActive = true;
            return job();
        };
        std::optional<int> r = callOnWorld<int>(rules(), deferred(), guarded, &how);
        if (how == WorldCallResult::Dropped)
            log(fl::LogLevel::Warn, "ForeverLua: Group member change dropped: the world thread queue is full");
        if (!r)
            return true;   // queued: no result yet
        if (*r == 0)
            return false;   // disbanded: "Group no longer exists"
        out.emplace_back(*r == 1);
        return true;
    }
    // The other group writes change state and send packets the same way: world thread only, queued from a map thread.
    if (self.kind == ObjKind::Group &&
        (name == "group.convertToLfg" || name == "group.convertToRaid" || name == "group.disband" ||
         name == "group.setLeader" || name == "group.setMemberFlag" || name == "group.setMembersGroup" ||
         name == "group.setTargetIcon"))
    {
        fl::Guid const groupGuid = self.guid;
        fl::Guid target;
        fl::Guid setter;
        bool const hasTarget = fl::parseGuidText(sArg(args, name == "group.setTargetIcon" ? 1 : 0), target);
        if (name == "group.setTargetIcon")
            fl::parseGuidText(sArg(args, 2), setter);
        bool const apply = bArg(args, 1, false);
        int const flag = iArg(args, name == "group.setTargetIcon" ? 0 : (name == "group.setMemberFlag" ? 2 : 1));
        auto job = [=]()
        {
            Group* g = sGroupMgr->GetGroupByGUID(toGuid(groupGuid));
            if (!g)
                return;
            if (name == "group.convertToLfg")
                g->ConvertToLFG();
            else if (name == "group.convertToRaid")
                g->ConvertToRaid();
            else if (name == "group.disband")
                g->Disband();   // frees the group: nothing below touches g afterwards
            else if (name == "group.setLeader")
            {
                if (hasTarget)
                    g->ChangeLeader(toGuid(target));
            }
            else if (name == "group.setMemberFlag")
            {
                if (hasTarget)
                    g->SetGroupMemberFlag(toGuid(target), apply, static_cast<GroupMemberFlags>(flag));
            }
            else if (name == "group.setMembersGroup")
            {
                if (hasTarget)
                    g->ChangeMembersGroup(toGuid(target), static_cast<uint8>(flag));
            }
            else if (hasTarget)
                g->SetTargetIcon(static_cast<uint8>(flag), toGuid(target), toGuid(setter));
        };
        runSocialWrite("Group change", job);
        return true;   // the docs: these return nothing, and a queued call has no result yet either
    }
    if (self.kind == ObjKind::Group)
    {
        Group* group = sGroupMgr->GetGroupByGUID(toGuid(self.guid));
        if (!group)
            return false;   // disbanded: "Group no longer exists"
        if (name == "group.getMembers")
        {
            for (GroupReference const& ref : group->GetMembers())
                if (Player* p = ref.GetSource())
                    if (p->IsInWorld())
                        pushHandle(out, handleFor(p));
            return true;
        }
        if (name == "group.getGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(group->GetGUID())));
            return true;
        }
        if (name == "group.getLeaderGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(group->GetLeaderGUID())));
            return true;
        }
        if (name == "group.getMembersCount")
        {
            out.emplace_back(static_cast<int64_t>(group->GetMembersCount()));
            return true;
        }
        if (name == "group.getMemberGuid")
        {
            ObjectGuid const guid = group->GetMemberGUID(sArg(args, 0));
            if (!guid.IsEmpty())
                out.emplace_back(fl::guidText(fromGuid(guid)));
            return true;
        }
        if (name == "group.getMemberFlags")
        {
            fl::Guid guid;
            if (fl::parseGuidText(sArg(args, 0), guid))
                out.emplace_back(static_cast<int64_t>(group->GetMemberFlags(toGuid(guid))));
            return true;
        }
        if (name == "group.getMemberGroup")
        {
            fl::Guid guid;
            if (fl::parseGuidText(sArg(args, 0), guid))
                out.emplace_back(static_cast<int64_t>(group->GetMemberGroup(toGuid(guid))));
            return true;
        }
        if (name == "group.isRaidGroup")
        {
            out.emplace_back(group->isRaidGroup());
            return true;
        }
        if (name == "group.isLfgGroup")
        {
            out.emplace_back(group->isLFGGroup());
            return true;
        }
        if (name == "group.isBgGroup")
        {
            out.emplace_back(group->isBGGroup());
            return true;
        }
        if (name == "group.isBfGroup")
        {
            out.emplace_back(group->isBFGroup());
            return true;
        }
        if (name == "group.isFull")
        {
            out.emplace_back(group->IsFull());
            return true;
        }
        if (name == "group.isMember" || name == "group.isLeader" || name == "group.isAssistant")
        {
            fl::Guid guid;
            if (fl::parseGuidText(sArg(args, 0), guid))
            {
                if (name == "group.isMember")
                    out.emplace_back(group->IsMember(toGuid(guid)));
                else if (name == "group.isLeader")
                    out.emplace_back(group->IsLeader(toGuid(guid)));
                else
                    out.emplace_back(group->IsAssistant(toGuid(guid)));
            }
            return true;
        }
        if (name == "group.sameSubGroup")
        {
            fl::Guid first;
            fl::Guid second;
            if (fl::parseGuidText(sArg(args, 0), first) && fl::parseGuidText(sArg(args, 1), second))
                out.emplace_back(group->SameSubGroup(toGuid(first), toGuid(second)));
            return true;
        }
        if (name == "group.hasFreeSlotSubGroup")
        {
            uint32 const subgroup = static_cast<uint32>(iArg(args, 0));
            if (subgroup < MAX_RAID_SUBGROUPS)   // the counters array the core reads has this many entries
                out.emplace_back(group->HasFreeSlotSubGroup(static_cast<uint8>(subgroup)));
            return true;
        }
        return false;
    }

    // ---- Guild ----
    // The guild writes go through the world thread for the same reason as the group ones (and AddMember/DeleteMember/
    // ChangeMemberRank take the character database transaction the caller commits).
    if (self.kind == ObjKind::Guild &&
        (name == "guild.addMember" || name == "guild.deleteMember" || name == "guild.disband" ||
         name == "guild.setMemberRank"))
    {
        fl::Guid const guildId = self.guid;
        HandleData player;
        bool const hasPlayer = asHandle(args.empty() ? EventValue() : args[0], player);
        flh::GuildWrite write;
        if (name == "guild.addMember")
        {
            write.kind = flh::GuildWriteKind::AddMember;
            write.hasRank = bArg(args, 2, false);
            write.rank = static_cast<uint32_t>(iArg(args, 1));
        }
        else if (name == "guild.deleteMember")
        {
            write.kind = flh::GuildWriteKind::DeleteMember;
            write.isDisbanding = bArg(args, 1, false);
        }
        else if (name == "guild.setMemberRank")
        {
            write.kind = flh::GuildWriteKind::ChangeRank;
            write.rank = static_cast<uint32_t>(iArg(args, 1));
        }
        auto job = [=]()
        {
            Guild* g = sGuildMgr->GetGuildById(static_cast<uint32>(guildId.lo));
            bool const needsPlayer = write.kind != flh::GuildWriteKind::Disband;
            Player* p = g && needsPlayer && hasPlayer ? findPlayer(player) : nullptr;
            // The core calls of the job, behind the game-free rule of GuildWrite.h.
            struct CoreGuild
            {
                Guild* guild;
                ObjectGuid guid;
                // The core adds the member at its lowest rank (no rank passed); an explicit rank is then set through
                // the public ChangeMemberRank, which refuses a rank the guild does not have (it looks it up itself).
                flh::GuildAddOutcome AddMember(bool hasRank, uint32_t rank)
                {
                    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                    if (!guild->AddMember(trans, guid, Optional<GuildRankId>()))
                        return flh::GuildAddOutcome::NotAdded;
                    flh::GuildAddOutcome outcome = flh::GuildAddOutcome::Added;
                    if (hasRank && !guild->ChangeMemberRank(trans, guid, static_cast<GuildRankId>(rank)))
                        outcome = flh::GuildAddOutcome::AddedAtDefaultRank;
                    CharacterDatabase.CommitTransaction(trans);
                    return outcome;
                }
                void DeleteMember(bool isDisbanding)
                {
                    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                    guild->DeleteMember(trans, guid, isDisbanding);
                    CharacterDatabase.CommitTransaction(trans);
                }
                void ChangeRank(uint32_t rank)
                {
                    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                    if (guild->ChangeMemberRank(trans, guid, static_cast<GuildRankId>(rank)))
                        CharacterDatabase.CommitTransaction(trans);
                }
                void Disband() { guild->Disband(); }
            } target{g, p ? p->GetGUID() : ObjectGuid::Empty};
            if (flh::applyGuildWrite(g ? &target : nullptr, p != nullptr, write) ==
                flh::GuildWriteResult::AddedAtDefaultRank)
                log(fl::LogLevel::Warn, "ForeverLua: Guild:AddMember: rank " + std::to_string(write.rank) +
                                            " does not exist; the member was added at the default rank");
        };
        runSocialWrite("Guild change", job);
        return true;
    }
    if (self.kind == ObjKind::Guild)
    {
        Guild* guild = sGuildMgr->GetGuildById(static_cast<uint32>(self.guid.lo));
        if (!guild)
            return false;   // disbanded: "Guild no longer exists"
        if (name == "guild.getName")
        {
            out.emplace_back(guild->GetName());
            return true;
        }
        if (name == "guild.getMotd")
        {
            out.emplace_back(guild->GetMOTD());
            return true;
        }
        if (name == "guild.getInfo")
        {
            out.emplace_back(guild->GetInfo());
            return true;
        }
        if (name == "guild.getId")
        {
            out.emplace_back(static_cast<int64_t>(guild->GetId()));
            return true;
        }
        if (name == "guild.getMemberCount")
        {
            out.emplace_back(static_cast<int64_t>(guild->GetMembersCount()));
            return true;
        }
        if (name == "guild.getMembers")
        {
            for (auto const& member : guild->GetMembers())
                if (Player* p = ObjectAccessor::FindPlayer(member.first))
                    if (p->IsInWorld())
                        pushHandle(out, handleFor(p));
            return true;
        }
        if (name == "guild.getLeaderGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(guild->GetLeaderGUID())));
            return true;
        }
        if (name == "guild.getLeader")
        {
            if (Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID()))
                if (leader->IsInWorld())
                    pushHandle(out, handleFor(leader));
            return true;
        }
        return false;
    }

    // ---- Quest ----
    if (self.kind == ObjKind::Quest)
    {
        uint32 const id = static_cast<uint32>(self.guid.lo);
        Quest const* quest = sObjectMgr->GetQuestTemplate(id);
        if (!quest)
            return false;   // "Quest no longer exists"
        if (name == "quest.getFlags")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetFlags()));
            return true;
        }
        if (name == "quest.getId")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetQuestId()));
            return true;
        }
        if (name == "quest.getLevel")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetClassicQuestLevel()));
            return true;
        }
        if (name == "quest.getMaxLevel")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetMaxLevel()));
            return true;
        }
        if (name == "quest.getMinLevel")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetClassicMinLevel()));
            return true;
        }
        if (name == "quest.getNextQuestId")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetNextQuestId()));
            return true;
        }
        if (name == "quest.getNextQuestInChain")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetNextQuestInChain()));
            return true;
        }
        if (name == "quest.getPrevQuestId")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetPrevQuestId()));
            return true;
        }
        if (name == "quest.getTitle")
        {
            out.emplace_back(quest->GetLogTitle());
            return true;
        }
        if (name == "quest.getType")
        {
            out.emplace_back(static_cast<int64_t>(quest->GetQuestType()));
            return true;
        }
        if (name == "quest.hasFlag")
        {
            out.emplace_back(quest->HasFlag(static_cast<QuestFlags>(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "quest.isDaily")
        {
            out.emplace_back(quest->IsDaily());
            return true;
        }
        if (name == "quest.isRepeatable")
        {
            out.emplace_back(quest->IsRepeatable());
            return true;
        }
        return false;
    }

    // ---- SpellInfo (a documented spell entry; core data, never stale) ----
    if (self.kind == ObjKind::SpellInfo)
    {
        uint32 const id = static_cast<uint32>(self.guid.lo);
        SpellInfo const* info = spellEntry(id);
        if (!info)
            return false;   // no handle exists for an id the core does not know
        for (SpellInfoNumOp const& op : kSpellInfoNumOps)
            if (name == op.op)
            {
                out.emplace_back(op.get(info));
                return true;
            }
        for (SpellInfoFloatOp const& op : kSpellInfoFloatOps)
            if (name == op.op)
            {
                out.emplace_back(op.get(info));
                return true;
            }
        for (SpellInfoBoolOp const& op : kSpellInfoBoolOps)
            if (name == op.op)
            {
                out.emplace_back(op.get(info));
                return true;
            }
        if (name == "spellinfo.getPowerType")
        {
            // The first power cost the spell has (docs: one power type).
            int8 power = POWER_MANA;
            for (SpellPowerEntry const* cost : info->PowerCosts)
                if (cost)
                {
                    power = cost->PowerType;
                    break;
                }
            out.emplace_back(static_cast<int64_t>(power));
            return true;
        }
        if (name == "spellinfo.getManaCost" || name == "spellinfo.getManaCostPerlevel" ||
            name == "spellinfo.getManaPerSecond")
        {
            // The mana entry of the spell's power costs (the core keeps the cost in a SpellPowerEntry row).
            SpellPowerEntry const* mana = manaCostOf(info);
            if (name == "spellinfo.getManaCost")
                out.emplace_back(static_cast<int64_t>(mana ? mana->ManaCost : 0));
            else if (name == "spellinfo.getManaCostPerlevel")
                out.emplace_back(static_cast<int64_t>(mana ? mana->ManaCostPerLevel : 0));
            else
                out.emplace_back(static_cast<int64_t>(mana ? mana->ManaPerSecond : 0));
            return true;
        }
        if (name == "spellinfo.getSpellFamilyFlags")
        {
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            if (index > 2)   // the docs' 96-bit split (index 0-2)
                throw std::out_of_range("spell family flags index " + std::to_string(index) + " is not 0..2");
            out.emplace_back(static_cast<int64_t>(static_cast<uint32>(info->SpellFamilyFlags[index])));
            return true;
        }
        if (name == "spellinfo.getTotem" || name == "spellinfo.getTotemCategory")
        {
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            if (index >= MAX_SPELL_TOTEMS)
                throw std::out_of_range("totem index " + std::to_string(index) + " is not 0.." +
                                        std::to_string(MAX_SPELL_TOTEMS - 1));
            if (name == "spellinfo.getTotem")
                out.emplace_back(static_cast<int64_t>(info->Totem[index]));
            else
                out.emplace_back(static_cast<int64_t>(info->TotemCategory[index]));
            return true;
        }
        if (name == "spellinfo.getReagent" || name == "spellinfo.getReagentCount")
        {
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            if (index >= MAX_SPELL_REAGENTS)
                throw std::out_of_range("reagent index " + std::to_string(index) + " is not 0.." +
                                        std::to_string(MAX_SPELL_REAGENTS - 1));
            if (name == "spellinfo.getReagent")
                out.emplace_back(static_cast<int64_t>(info->Reagent[index]));
            else
                out.emplace_back(static_cast<int64_t>(info->ReagentCount[index]));
            return true;
        }
        if (name == "spellinfo.hasEffect" || name == "spellinfo.hasAura")
        {
            int64 const value = iArg(args, 0);
            if (value < 0)
                throw std::out_of_range("a spell effect or aura id is not negative");
            if (name == "spellinfo.hasEffect")
                out.emplace_back(value < TOTAL_SPELL_EFFECTS &&
                                 info->HasEffect(static_cast<SpellEffects>(value)));
            else
                out.emplace_back(value < TOTAL_AURAS && info->HasAura(static_cast<AuraType>(value)));
            return true;
        }
        if (name == "spellinfo.isPositiveEffect")
        {
            // The core's positivity bitset has one bit per effect slot (MAX_SPELL_EFFECTS).
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            if (index >= MAX_SPELL_EFFECTS)
                throw std::out_of_range("effect index " + std::to_string(index) + " is not below MAX_SPELL_EFFECTS");
            out.emplace_back(info->IsPositiveEffect(static_cast<uint8>(index)));
            return true;
        }
        if (name == "spellinfo.canBeUsedInCombat")
        {
            // The docs' method takes no caster. The core's own answer needs one for the mount exception, so without a
            // caster the not-in-combat attribute alone decides (the conservative answer: no caster, no exception).
            out.emplace_back(!info->HasAttribute(SPELL_ATTR0_NOT_IN_COMBAT_ONLY_PEACEFUL));
            return true;
        }
        if (name == "spellinfo.needsToBeTriggeredByCaster" || name == "spellinfo.isRankOf" ||
            name == "spellinfo.isDifferentRankOf" || name == "spellinfo.isHighRankOf" ||
            name == "spellinfo.canDispelAura" || name == "spellinfo.canPierceImmuneAura" ||
            name == "spellinfo.canSpellProvideImmunityAgainstAura" ||
            name == "spellinfo.isAuraExclusiveBySpecificWith" ||
            name == "spellinfo.isAuraExclusiveBySpecificPerCasterWith")
        {
            SpellInfo const* other = spellEntry(static_cast<uint32>(iArg(args, 0)));
            if (!other)
                throw std::out_of_range("the spell info argument names no spell the server knows");
            if (name == "spellinfo.needsToBeTriggeredByCaster")
                out.emplace_back(info->NeedsToBeTriggeredByCaster(other));
            else if (name == "spellinfo.isRankOf")
                out.emplace_back(info->IsRankOf(other));
            else if (name == "spellinfo.isDifferentRankOf")
                out.emplace_back(info->IsDifferentRankOf(other));
            else if (name == "spellinfo.isHighRankOf")
                out.emplace_back(info->IsHighRankOf(other));
            else if (name == "spellinfo.canDispelAura")
                out.emplace_back(info->CanDispelAura(other));
            else if (name == "spellinfo.canPierceImmuneAura")
                out.emplace_back(info->CanPierceImmuneAura(other));
            else if (name == "spellinfo.isAuraExclusiveBySpecificWith")
                out.emplace_back(info->IsAuraExclusiveBySpecificWith(other));
            else if (name == "spellinfo.isAuraExclusiveBySpecificPerCasterWith")
                out.emplace_back(info->IsAuraExclusiveBySpecificPerCasterWith(other));
            else
                out.emplace_back(info->CanSpellProvideImmunityAgainstAura(other));
            return true;
        }
        if (name == "spellinfo.getManaCostPercentage")
        {
            SpellPowerEntry const* mana = manaCostOf(info);
            out.emplace_back(static_cast<int64_t>(mana ? static_cast<uint32>(mana->PowerCostPct) : 0));
            return true;
        }
        if (name == "spellinfo.getMaxTicks")
        {
            uint32 maxTicks = 0;
            for (SpellEffectInfo const& eff : info->GetEffects())
                maxTicks = std::max(maxTicks, eff.GetPeriodicTickCount());
            out.emplace_back(static_cast<int64_t>(maxTicks));
            return true;
        }
        if (name == "spellinfo.getSpellMechanicMaskByEffectMask")
        {
            out.emplace_back(static_cast<int64_t>(
                info->GetSpellMechanicMaskByEffectMask(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "spellinfo.getSpellVisual")
        {
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            if (index > 1)
                throw std::out_of_range("spell visual index " + std::to_string(index) + " is not 0..1");
            out.emplace_back(static_cast<int64_t>(index == 0 ? info->GetSpellVisual() : 0));
            return true;
        }
        if (name == "spellinfo.isAbilityLearnedWithProfession")
        {
            bool learned = false;
            SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(info->Id);
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
            {
                if (IsProfessionSkill(itr->second->SkillLine) &&
                    itr->second->GetAcquireMethod() == SkillLineAbilityAcquireMethod::AutomaticSkillRank)
                {
                    learned = true;
                    break;
                }
            }
            out.emplace_back(learned);
            return true;
        }
        if (name == "spellinfo.isAbilityOfSkillType")
        {
            out.emplace_back(info->IsAbilityOfSkillType(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "spellinfo.isAffected")
        {
            flag128 flags;
            flags[0] = static_cast<uint32>(iArg(args, 1));
            flags[1] = static_cast<uint32>(iArg(args, 2));
            flags[2] = static_cast<uint32>(iArg(args, 3));
            out.emplace_back(info->IsAffected(static_cast<uint32>(iArg(args, 0)), flags));
            return true;
        }
        if (name == "spellinfo.isProfessionOrRiding")
        {
            bool ok = false;
            for (SpellEffectInfo const& eff : info->GetEffects())
                if (eff.Effect == SPELL_EFFECT_SKILL && IsProfessionOrRidingSkill(eff.MiscValue))
                {
                    ok = true;
                    break;
                }
            out.emplace_back(ok);
            return true;
        }
        if (name == "spellinfo.needsComboPoints")
        {
            bool needs = info->HasAttribute(SPELL_ATTR1_FINISHING_MOVE_DAMAGE) ||
                         info->HasAttribute(SPELL_ATTR1_FINISHING_MOVE_DURATION);
            if (!needs)
                for (SpellPowerEntry const* cost : info->PowerCosts)
                    if (cost && cost->PowerType == POWER_COMBO_POINTS)
                    {
                        needs = true;
                        break;
                    }
            out.emplace_back(needs);
            return true;
        }
        if (name == "spellinfo.checkTarget")
        {
            HandleData casterH, targetH;
            asHandle(args[0], casterH);
            asHandle(args[1], targetH);
            WorldObject* caster = findObject(casterH);
            WorldObject* target = findObject(targetH);
            if (!caster || !target || caster->GetMap() != target->GetMap())
                out.emplace_back(static_cast<int64_t>(SPELL_FAILED_BAD_TARGETS));
            else
                out.emplace_back(static_cast<int64_t>(info->CheckTarget(caster, target)));
            return true;
        }
        if (name == "spellinfo.checkTargetCreatureType")
        {
            HandleData targetH;
            asHandle(args[0], targetH);
            Unit* target = findUnit(targetH);
            out.emplace_back(target ? info->CheckTargetCreatureType(target) : false);
            return true;
        }
        if (name == "spellinfo.isItemFitToSpellRequirements")
        {
            HandleData itemH;
            asHandle(args[0], itemH);
            Item* item = findItem(itemH);
            out.emplace_back(info->IsItemFitToSpellRequirements(item));
            return true;
        }
        if (name == "spellinfo.spellCancelsAuraEffect")
        {
            AuraEffect* eff = auraEffectById(static_cast<uint32>(iArg(args, 0)));
            if (!eff)
                throw std::out_of_range("AuraEffect no longer exists");
            out.emplace_back(info->SpellCancelsAuraEffect(eff));
            return true;
        }
        if (name.rfind("spellinfo.effectIs", 0) == 0 || name.rfind("spellinfo.getEffect", 0) == 0)
        {
            uint32 const effIdx = static_cast<uint32>(iArg(args, 0));
            SpellEffectInfo const& eff = effectAt(info, effIdx);
            if (name == "spellinfo.effectIsAreaAuraEffect") out.emplace_back(eff.IsAreaAuraEffect());
            else if (name == "spellinfo.effectIsAura") out.emplace_back(eff.IsAura());
            else if (name == "spellinfo.effectIsAuraType")
            {
                int64 const aura = iArg(args, 1);
                out.emplace_back(aura >= 0 && aura < TOTAL_AURAS && eff.IsAura(static_cast<AuraType>(aura)));
            }
            else if (name == "spellinfo.effectIsEffect") out.emplace_back(eff.IsEffect());
            else if (name == "spellinfo.effectIsEffectType")
            {
                int64 const effect = iArg(args, 1);
                out.emplace_back(effect >= 0 && effect < TOTAL_SPELL_EFFECTS &&
                                 eff.IsEffect(static_cast<SpellEffects>(effect)));
            }
            else if (name == "spellinfo.effectIsTargetingArea") out.emplace_back(eff.IsTargetingArea());
            else if (name == "spellinfo.effectIsUnitOwnedAuraEffect") out.emplace_back(eff.IsUnitOwnedAuraEffect());
            else if (name == "spellinfo.getEffectAmplitude") out.emplace_back(static_cast<int64_t>(eff.ApplyAuraPeriod));
            else if (name == "spellinfo.getEffectApplyAuraName") out.emplace_back(static_cast<int64_t>(eff.ApplyAuraName));
            else if (name == "spellinfo.getEffectBasePoints") out.emplace_back(static_cast<int64_t>(eff.BasePoints));
            else if (name == "spellinfo.getEffectBonusMultiplier") out.emplace_back(static_cast<double>(eff.BonusCoefficient));
            else if (name == "spellinfo.getEffectCalcRadius") out.emplace_back(static_cast<double>(eff.CalcRadius().Max));
            else if (name == "spellinfo.getEffectCalcValue") out.emplace_back(static_cast<int64_t>(eff.CalcValueAsInt()));
            else if (name == "spellinfo.getEffectChainTarget") out.emplace_back(static_cast<int64_t>(eff.ChainTargets));
            else if (name == "spellinfo.getEffectDamageMultiplier")
                out.emplace_back(static_cast<double>(eff.CalcDamageMultiplier(nullptr)));
            else if (name == "spellinfo.getEffectDieSides")
            {
                uint32 const vanillaId = sSpellMgr->GetClassicValueSpellId(info->Id);
                ClassicSpellValue::Value const* vanilla =
                    ClassicSpellValue::AppliesToSpell(vanillaId) ? sSpellMgr->GetClassicSpellValue(vanillaId, effIdx)
                                                                 : nullptr;
                out.emplace_back(static_cast<int64_t>(vanilla ? vanilla->DieSides : 0));
            }
            else if (name == "spellinfo.getEffectHasRadius")
                out.emplace_back(eff.HasRadius(SpellTargetIndex::TargetA));
            else if (name == "spellinfo.getEffectImplicitTargetType")
                out.emplace_back(static_cast<int64_t>(eff.GetImplicitTargetType()));
            else if (name == "spellinfo.getEffectIndex") out.emplace_back(static_cast<int64_t>(eff.EffectIndex));
            else if (name == "spellinfo.getEffectItemType") out.emplace_back(static_cast<int64_t>(eff.ItemType));
            else if (name == "spellinfo.getEffectMechanic")
                out.emplace_back(static_cast<int64_t>(info->GetEffectMechanic(static_cast<SpellEffIndex>(effIdx))));
            else if (name == "spellinfo.getEffectMechanicMask")
                out.emplace_back(static_cast<int64_t>(info->GetEffectMechanicMask(static_cast<SpellEffIndex>(effIdx))));
            else if (name == "spellinfo.getEffectMiscValue") out.emplace_back(static_cast<int64_t>(eff.MiscValue));
            else if (name == "spellinfo.getEffectMiscValueB") out.emplace_back(static_cast<int64_t>(eff.MiscValueB));
            else if (name == "spellinfo.getEffectPointsPerComboPoint")
                out.emplace_back(static_cast<double>(eff.PointsPerResource));
            else if (name == "spellinfo.getEffectProvidedTargetMask")
                out.emplace_back(static_cast<int64_t>(eff.GetProvidedTargetMask()));
            else if (name == "spellinfo.getEffectRealPointsPerLevel")
                out.emplace_back(static_cast<double>(eff.RealPointsPerLevel));
            else if (name == "spellinfo.getEffectSpellClassMask")
            {
                uint32 const maskIdx = static_cast<uint32>(iArg(args, 1));
                if (maskIdx > 2)
                    throw std::out_of_range("spell class mask index " + std::to_string(maskIdx) + " is not 0..2");
                out.emplace_back(static_cast<int64_t>(static_cast<uint32>(eff.SpellClassMask[maskIdx])));
            }
            else if (name == "spellinfo.getEffectTargetACheckType")
                out.emplace_back(static_cast<int64_t>(eff.TargetA.GetCheckType()));
            else if (name == "spellinfo.getEffectTargetADirectionAngle")
                out.emplace_back(static_cast<double>(eff.TargetA.CalcDirectionAngle()));
            else if (name == "spellinfo.getEffectTargetADirectionType")
                out.emplace_back(static_cast<int64_t>(eff.TargetA.GetDirectionType()));
            else if (name == "spellinfo.getEffectTargetAIsArea") out.emplace_back(eff.TargetA.IsArea());
            else if (name == "spellinfo.getEffectTargetAObjectType")
                out.emplace_back(static_cast<int64_t>(eff.TargetA.GetObjectType()));
            else if (name == "spellinfo.getEffectTargetAReferenceType")
                out.emplace_back(static_cast<int64_t>(eff.TargetA.GetReferenceType()));
            else if (name == "spellinfo.getEffectTargetASelectionCategory")
                out.emplace_back(static_cast<int64_t>(eff.TargetA.GetSelectionCategory()));
            else if (name == "spellinfo.getEffectTargetATarget")
                out.emplace_back(static_cast<int64_t>(eff.TargetA.GetTarget()));
            else if (name == "spellinfo.getEffectTargetBCheckType")
                out.emplace_back(static_cast<int64_t>(eff.TargetB.GetCheckType()));
            else if (name == "spellinfo.getEffectTargetBDirectionAngle")
                out.emplace_back(static_cast<double>(eff.TargetB.CalcDirectionAngle()));
            else if (name == "spellinfo.getEffectTargetBDirectionType")
                out.emplace_back(static_cast<int64_t>(eff.TargetB.GetDirectionType()));
            else if (name == "spellinfo.getEffectTargetBIsArea") out.emplace_back(eff.TargetB.IsArea());
            else if (name == "spellinfo.getEffectTargetBObjectType")
                out.emplace_back(static_cast<int64_t>(eff.TargetB.GetObjectType()));
            else if (name == "spellinfo.getEffectTargetBReferenceType")
                out.emplace_back(static_cast<int64_t>(eff.TargetB.GetReferenceType()));
            else if (name == "spellinfo.getEffectTargetBSelectionCategory")
                out.emplace_back(static_cast<int64_t>(eff.TargetB.GetSelectionCategory()));
            else if (name == "spellinfo.getEffectTargetBTarget")
                out.emplace_back(static_cast<int64_t>(eff.TargetB.GetTarget()));
            else if (name == "spellinfo.getEffectTriggerSpell") out.emplace_back(static_cast<int64_t>(eff.TriggerSpell));
            else if (name == "spellinfo.getEffectType") out.emplace_back(static_cast<int64_t>(eff.Effect));
            else if (name == "spellinfo.getEffectUsedTargetObjectType")
                out.emplace_back(static_cast<int64_t>(eff.GetUsedTargetObjectType()));
            else if (name == "spellinfo.getEffectValueMultiplier")
                out.emplace_back(static_cast<double>(eff.CalcValueMultiplier(nullptr)));
            else
                return false;
            return true;
        }
        return false;
    }

    if (self.kind == ObjKind::AuraEffect)
    {
        AuraEffect* eff = auraEffectById(self.instanceId);
        if (!eff)
            return false;   // "AuraEffect no longer exists"
        if (name == "auraeffect.calculateAmount")
        {
            HandleData h;
            asHandle(args[0], h);
            Unit* caster = findUnit(h);
            if (!caster)
                throw std::out_of_range("Unit no longer exists");
            out.emplace_back(static_cast<int64_t>(eff->CalculateAmount(caster)));
            return true;
        }
        if (name == "auraeffect.calculatePeriodic")
        {
            HandleData h;
            asHandle(args[0], h);
            Unit* caster = findUnit(h);
            if (!caster)
                throw std::out_of_range("Unit no longer exists");
            eff->CalculatePeriodic(caster, bArg(args, 1, false), bArg(args, 2, false));
            return true;
        }
        if (name == "auraeffect.canBeRecalculated")
        {
            out.emplace_back(eff->CanBeRecalculated());
            return true;
        }
        if (name == "auraeffect.changeAmount")
        {
            eff->ChangeAmount(static_cast<SpellEffectValue>(iArg(args, 0)), bArg(args, 1, true),
                              bArg(args, 2, false));
            return true;
        }
        if (name == "auraeffect.getAmount")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetAmountAsInt()));
            return true;
        }
        if (name == "auraeffect.getAmplitude")
        {
            out.emplace_back(static_cast<int64_t>(std::max<int32>(0, eff->GetPeriod())));
            return true;
        }
        if (name == "auraeffect.getAuraType")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetAuraType()));
            return true;
        }
        if (name == "auraeffect.getBase")
        {
            if (Aura* base = eff->GetBase())
            {
                fl::HandleData h;
                h.kind = ObjKind::Aura;
                h.guid = base->GetCaster() ? fromGuid(base->GetCaster()->GetGUID()) : fl::Guid{};
                h.mapId = base->GetId();
                h.instanceId = rememberAura(base);
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "auraeffect.getBaseAmount")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetBaseAmount()));
            return true;
        }
        if (name == "auraeffect.getCaster")
        {
            if (auto h = liveHandle(eff->GetCaster()))
                pushHandle(out, *h);
            return true;
        }
        if (name == "auraeffect.getCasterGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(eff->GetCasterGUID())));
            return true;
        }
        if (name == "auraeffect.getEffIndex")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetEffIndex()));
            return true;
        }
        if (name == "auraeffect.getId")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetId()));
            return true;
        }
        if (name == "auraeffect.getPeriodicTimer")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetPeriodicTimer()));
            return true;
        }
        if (name == "auraeffect.getRemainingTicks")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetRemainingTicks()));
            return true;
        }
        if (name == "auraeffect.getSpellInfo")
        {
            if (SpellInfo const* info = eff->GetSpellInfo())
            {
                fl::HandleData h;
                h.kind = ObjKind::SpellInfo;
                h.guid.lo = info->Id;
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "auraeffect.getTargetList")
        {
            std::vector<Unit*> targets;
            eff->GetTargetList(targets);
            for (Unit* u : targets)
                if (auto h = liveHandle(u))
                    pushHandle(out, *h);
            return true;
        }
        if (name == "auraeffect.getTickNumber")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetTickNumber()));
            return true;
        }
        if (name == "auraeffect.getTotalTicks")
        {
            out.emplace_back(static_cast<int64_t>(eff->GetTotalTicks()));
            return true;
        }
        if (name == "auraeffect.recalculateAmount")
        {
            eff->RecalculateAmount();
            return true;
        }
        if (name == "auraeffect.setAmount")
        {
            eff->SetAmount(static_cast<SpellEffectValue>(iArg(args, 0)));
            return true;
        }
        if (name == "auraeffect.setCanBeRecalculated")
        {
            eff->SetCanBeRecalculated(bArg(args, 0, true));
            return true;
        }
        if (name == "auraeffect.setPeriodicTimer")
        {
            eff->SetPeriodicTimer(static_cast<int32>(iArg(args, 0)));
            return true;
        }
        return false;
    }

    if (self.kind == ObjKind::ElunaProcInfo)
    {
        ProcEventInfo* proc = procInfoById(self.instanceId);
        if (!proc)
            return false;   // "ElunaProcInfo no longer exists"
        if (name == "procinfo.getActionTarget")
        {
            if (auto h = liveHandle(proc->GetActionTarget()))
                pushHandle(out, *h);
            return true;
        }
        if (name == "procinfo.getActor")
        {
            if (auto h = liveHandle(proc->GetActor()))
                pushHandle(out, *h);
            return true;
        }
        if (name == "procinfo.getAttackType")
        {
            out.emplace_back(static_cast<int64_t>(
                proc->GetDamageInfo() ? proc->GetDamageInfo()->GetAttackType() : BASE_ATTACK));
            return true;
        }
        if (name == "procinfo.getBlock")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetDamageInfo() ? proc->GetDamageInfo()->GetBlock() : 0));
            return true;
        }
        if (name == "procinfo.getDamage")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetDamageInfo() ? proc->GetDamageInfo()->GetDamage() : 0));
            return true;
        }
        if (name == "procinfo.getDamageAbsorb")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetDamageInfo() ? proc->GetDamageInfo()->GetAbsorb() : 0));
            return true;
        }
        if (name == "procinfo.getDamageType")
        {
            out.emplace_back(static_cast<int64_t>(
                proc->GetDamageInfo() ? proc->GetDamageInfo()->GetDamageType() : DIRECT_DAMAGE));
            return true;
        }
        if (name == "procinfo.getEffectiveHeal")
        {
            out.emplace_back(static_cast<int64_t>(
                proc->GetHealInfo() ? proc->GetHealInfo()->GetEffectiveHeal() : 0));
            return true;
        }
        if (name == "procinfo.getHeal")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetHealInfo() ? proc->GetHealInfo()->GetHeal() : 0));
            return true;
        }
        if (name == "procinfo.getHealAbsorb")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetHealInfo() ? proc->GetHealInfo()->GetAbsorb() : 0));
            return true;
        }
        if (name == "procinfo.getHitMask")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetHitMask()));
            return true;
        }
        if (name == "procinfo.getMap")
        {
            Unit* ref = proc->GetActor() ? proc->GetActor() : proc->GetActionTarget();
            if (Map* m = (ref ? ref->FindMap() : nullptr))
                pushHandle(out, mapHandle(m->GetId(), m->GetInstanceId()));
            return true;
        }
        if (name == "procinfo.getProcSpell")
        {
            if (Spell const* sp = proc->GetProcSpell())
                pushHandle(out, handleForSpell(const_cast<Spell*>(sp)));
            return true;
        }
        if (name == "procinfo.getResist")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetDamageInfo() ? proc->GetDamageInfo()->GetResist() : 0));
            return true;
        }
        if (name == "procinfo.getSchoolMask")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetSchoolMask()));
            return true;
        }
        if (name == "procinfo.getSpellInfo")
        {
            if (SpellInfo const* info = proc->GetSpellInfo())
            {
                fl::HandleData h;
                h.kind = ObjKind::SpellInfo;
                h.guid.lo = info->Id;
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "procinfo.getSpellPhaseMask")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetSpellPhaseMask()));
            return true;
        }
        if (name == "procinfo.getSpellTypeMask")
        {
            out.emplace_back(static_cast<int64_t>(proc->GetSpellTypeMask()));
            return true;
        }
        if (name == "procinfo.getTypeMask")
        {
            out.emplace_back(static_cast<int64_t>(static_cast<uint32>(proc->GetTypeMask()[0])));
            return true;
        }
        if (name == "procinfo.hasDamage")
        {
            out.emplace_back(proc->GetDamageInfo() != nullptr);
            return true;
        }
        if (name == "procinfo.hasHeal")
        {
            out.emplace_back(proc->GetHealInfo() != nullptr);
            return true;
        }
        if (name == "procinfo.setAbsorbDamage")
        {
            if (DamageInfo* dmg = proc->GetDamageInfo())
                dmg->AbsorbDamage(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "procinfo.setAbsorbHeal")
        {
            if (HealInfo* heal = proc->GetHealInfo())
                heal->AbsorbHeal(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "procinfo.setBlockDamage")
        {
            if (DamageInfo* dmg = proc->GetDamageInfo())
                dmg->BlockDamage(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "procinfo.setDamage")
        {
            if (DamageInfo* dmg = proc->GetDamageInfo())
            {
                int64_t const target = iArg(args, 0);
                int64_t const cur = static_cast<int64_t>(dmg->GetDamage());
                int64_t const delta =
                    std::clamp(target - cur, -cur, static_cast<int64_t>(std::numeric_limits<int32>::max()));
                dmg->ModifyDamage(static_cast<int32>(delta));
            }
            return true;
        }
        if (name == "procinfo.setEffectiveHeal")
        {
            if (HealInfo* heal = proc->GetHealInfo())
                heal->SetEffectiveHeal(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "procinfo.setResistDamage")
        {
            if (DamageInfo* dmg = proc->GetDamageInfo())
                dmg->ResistDamage(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        return false;
    }

    // ---- Spell / Aura (transient objects of the running Lua call) ----
    if (self.kind == ObjKind::Spell)
    {
        Spell* spell = spellById(self.instanceId);
        if (!spell)
            return false;   // "Spell no longer exists"
        if (name == "spell.getEntry")
        {
            if (SpellInfo const* info = spell->GetSpellInfo())
                out.emplace_back(static_cast<int64_t>(info->Id));
            return true;
        }
        if (name == "spell.getCaster")
        {
            if (auto h = liveHandle(spell->GetCaster()))
                pushHandle(out, *h);
            return true;
        }
        if (name == "spell.getOriginalCaster")
        {
            // Never the cached m_originalCaster pointer (Spell.h: "updated at Spell::UpdatePointers()"): between two
            // updates it can name a unit the core has already destroyed. Spell::UpdateOriginalCasterPointer resolves
            // this same guid (an original caster out of the caster's map is nil there too).
            Unit* original = ObjectAccessor::GetUnit(*spell->GetCaster(), spell->GetOriginalCasterGUID());
            if (auto h = liveHandle(original))
                pushHandle(out, *h);
            return true;
        }
        if (name == "spell.getSpellInfo")
        {
            if (SpellInfo const* info = spell->GetSpellInfo())
            {
                fl::HandleData h;
                h.kind = ObjKind::SpellInfo;
                h.guid.lo = info->Id;
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "spell.getCastTime")
        {
            out.emplace_back(static_cast<int64_t>(spell->GetCastTime()));
            return true;
        }
        if (name == "spell.getDelayStart")
        {
            out.emplace_back(static_cast<int64_t>(spell->GetDelayStart()));
            return true;
        }
        if (name == "spell.getDelayMoment")
        {
            out.emplace_back(static_cast<int64_t>(spell->GetDelayMoment()));
            return true;
        }
        if (name == "spell.setDelayStart")
        {
            int64 const time = iArg(args, 0);
            if (time < 0)
                throw std::out_of_range("a delay start time is not negative");
            spell->SetDelayStart(static_cast<uint64>(time));
            return true;
        }
        if (name == "spell.getPowerCost")
        {
            // The docs give one number: the amount of the spell's first power cost (0 when the spell has none).
            std::vector<SpellPowerCost> const& costs = spell->GetPowerCost();
            out.emplace_back(costs.empty() ? int64_t(0) : static_cast<int64_t>(costs.front().Amount));
            return true;
        }
        if (name == "spell.getState")
        {
            out.emplace_back(static_cast<int64_t>(spell->getState()));
            return true;
        }
        if (name == "spell.setState")
        {
            spell->setState(static_cast<SpellState>(iArg(args, 0)));
            return true;
        }
        if (name == "spell.getTarget")
        {
            // Resolved from the guids, the way SpellCastTargets::Update does, never from the cached pointers it keeps
            // (they are refreshed only at cast/update, so between two ticks they can name a freed object).
            WorldObject* target =
                ObjectAccessor::GetWorldObject(*spell->GetCaster(), spell->m_targets.GetObjectTargetGUID());
            if (auto h = targetHandle(target))
                pushHandle(out, *h);
            else
            {
                Item* item = nullptr;
                ObjectGuid const itemGuid = spell->m_targets.GetItemTargetGUID();
                if (!itemGuid.IsEmpty())
                {
                    if (itemGuid == ObjectGuid::TradeItem)
                    {
                        // The trade slot's item has no inventory position; while the guid is still the special
                        // TradeItem id, SpellCastTargets::Update finds it in the trade (Spell.cpp:413-418). Looked up
                        // the same way instead of trusting the cached m_itemTarget pointer, which can name a freed item
                        // after the trade closed.
                        if (Player* caster = spell->GetCaster()->ToPlayer())
                            if (TradeData* trade = caster->GetTradeData())
                                item = trade->GetTraderData()->GetItem(TRADE_SLOT_NONTRADED);
                    }
                    else if (Player* caster = spell->GetCaster()->ToPlayer())
                        item = caster->GetItemByGuid(itemGuid);
                }
                if (item)
                {
                    rememberItem(item);
                    pushHandle(out, handleForItem(item));
                }
            }
            return true;
        }
        if (name == "spell.getTargetDest")
        {
            // The accessor always answers the destination position; a spell that targets a unit only never set one, so
            // the three numbers the docs promise are 0,0,0 there.
            WorldLocation const* dst = spell->m_targets.GetDstPos();
            out.emplace_back(static_cast<double>(dst->GetPositionX()));
            out.emplace_back(static_cast<double>(dst->GetPositionY()));
            out.emplace_back(static_cast<double>(dst->GetPositionZ()));
            return true;
        }
        if (name == "spell.isAutoRepeat")
        {
            out.emplace_back(spell->IsAutoRepeat());
            return true;
        }
        if (name == "spell.setAutoRepeat")
        {
            spell->SetAutoRepeat(bArg(args, 0, false));
            return true;
        }
        if (name == "spell.cast")
        {
            // The core runs a cast only from PREPARING: Spell::prepare's own two cast sites (Spell.cpp:3630, 3662) and
            // Spell::update's PREPARING case (Spell.cpp:4353). In every other state the cast either has not started
            // (NULL/IDLE) or has already run (LAUNCHED/CHANNELING/FINISHED), so a second _cast re-executes effects and
            // re-applies auras on a spell the core considers done.
            if (spell->getState() != SPELL_STATE_PREPARING)
                throw std::out_of_range("spell state " + std::to_string(spell->getState()) +
                                        " cannot be cast (the core casts a spell from PREPARING only)");
            spell->cast(bArg(args, 0, false));
            return true;
        }
        if (name == "spell.finish")
        {
            spell->finish();
            return true;
        }
        if (name == "spell.cancel")
        {
            spell->cancel();
            return true;
        }
        if (name == "spell.getDuration")
        {
            if (SpellInfo const* info = spell->GetSpellInfo())
                out.emplace_back(static_cast<int64_t>(info->GetDuration()));
            return true;
        }
        if (name == "spell.isPositive")
        {
            out.emplace_back(spell->IsPositive());
            return true;
        }
        if (name == "spell.isTriggered")
        {
            out.emplace_back(spell->IsTriggered());
            return true;
        }
        if (name == "spell.isChannelActive")
        {
            out.emplace_back(spell->IsChannelActive());
            return true;
        }
        if (name == "spell.isIgnoringCooldowns")
        {
            out.emplace_back(spell->IsIgnoringCooldowns());
            return true;
        }
        if (name == "spell.isInterruptable")
        {
            out.emplace_back(spell->IsInterruptable());
            return true;
        }
        if (name == "spell.isNeedSendToClient")
        {
            out.emplace_back(spell->IsNeedSendToClient());
            return true;
        }
        if (name == "spell.isProcDisabled")
        {
            out.emplace_back(spell->IsProcDisabled());
            return true;
        }
        if (name == "spell.isDeletable")
        {
            out.emplace_back(spell->IsDeletable());
            return true;
        }
        if (name == "spell.isAutoActionResetSpell")
        {
            out.emplace_back(spell->IsAutoActionResetSpell());
            return true;
        }
        if (name == "spell.isTriggeredByAura")
        {
            SpellInfo const* auraInfo = spellEntry(static_cast<uint32>(iArg(args, 0)));
            out.emplace_back(auraInfo && spell->IsTriggeredByAura(auraInfo));
            return true;
        }
        if (name == "spell.setExecutedCurrently")
        {
            spell->SetExecutedCurrently(bArg(args, 0, false));
            return true;
        }
        if (name == "spell.setReferencedFromCurrent")
        {
            spell->SetReferencedFromCurrent(bArg(args, 0, false));
            return true;
        }
        if (name == "spell.getRuneState")
        {
            out.emplace_back(static_cast<int64_t>(spell->GetRuneState()));
            return true;
        }
        if (name == "spell.setRuneState")
        {
            spell->SetRuneState(static_cast<uint8>(iArg(args, 0)));
            return true;
        }
        if (name == "spell.setSpellValue")
        {
            // SpellValueMod runs 0..SPELLVALUE_DURATION_PCT; the base points and the last three mods take a float.
            int64 const mod = iArg(args, 0);
            int64 const value = iArg(args, 1);
            if (mod < 0 || mod > SPELLVALUE_DURATION_PCT)
                throw std::out_of_range("spell value mod " + std::to_string(mod) + " is not 0.." +
                                        std::to_string(SPELLVALUE_DURATION_PCT));
            if (mod >= SPELLVALUE_BASE_POINT0)
                spell->SetSpellValue(CastSpellExtraArgsInit::SpellValueOverride(
                    static_cast<SpellValueModFloat>(mod), static_cast<SpellEffectValue>(value)));
            else if (mod < SPELLVALUE_INT_END)
                spell->SetSpellValue(
                    CastSpellExtraArgsInit::SpellValueOverride(static_cast<SpellValueMod>(mod), static_cast<int32>(value)));
            else
                spell->SetSpellValue(CastSpellExtraArgsInit::SpellValueOverride(
                    static_cast<SpellValueModFloat>(mod), static_cast<SpellEffectValue>(value)));
            return true;
        }
        if (name == "spell.updatePointers")
        {
            out.emplace_back(spell->UpdatePointers());
            return true;
        }
        if (name == "spell.cleanupTargetList")
        {
            spell->CleanupTargetList();
            return true;
        }
        if (name == "spell.calculateDamage")
        {
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return false;
            SpellEffectInfo const& eff = effectAt(info, index);
            // Never the cached m_objectTarget pointer SpellCastTargets::GetUnitTarget reads (Spell.cpp:191): it is
            // refreshed only in SpellCastTargets::Update, so between two ticks it can name a freed unit. Resolved from
            // the guid the way SpellCastTargets::Update does; a gone target is null, which CalcValue allows
            // (SpellInfo.cpp:781 CalcBaseValue null-checks it).
            ObjectGuid const targetGuid = spell->m_targets.GetUnitTargetGUID();
            Unit* target = targetGuid.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*spell->GetCaster(), targetGuid);
            SpellEffectValue const damage = spell->CalculateDamage(eff, target);
            out.emplace_back(static_cast<int64_t>(static_cast<int32>(std::clamp(damage, -2147483647.0, 2147483647.0))));
            return true;
        }
        if (name == "spell.getUnitTargetCountForEffect" || name == "spell.getGameObjectTargetCountForEffect" ||
            name == "spell.getItemTargetCountForEffect")
        {
            // (1 << effect) is only defined below MAX_SPELL_EFFECTS in the core's counter.
            uint32 const index = static_cast<uint32>(iArg(args, 0));
            if (index >= MAX_SPELL_EFFECTS)
                throw std::out_of_range("effect index " + std::to_string(index) + " is not below MAX_SPELL_EFFECTS");
            SpellEffIndex const eff = static_cast<SpellEffIndex>(index);
            if (name == "spell.getUnitTargetCountForEffect")
                out.emplace_back(spell->GetUnitTargetCountForEffect(eff));
            else if (name == "spell.getGameObjectTargetCountForEffect")
                out.emplace_back(spell->GetGameObjectTargetCountForEffect(eff));
            else
                out.emplace_back(spell->GetItemTargetCountForEffect(eff));
            return true;
        }
        return false;
    }
    if (self.kind == ObjKind::Aura)
    {
        Aura* aura = auraById(self.instanceId);
        if (!aura)
            return false;   // "Aura no longer exists"
        if (name == "aura.getAuraId" || name == "aura.getEntry")
        {
            out.emplace_back(static_cast<int64_t>(aura->GetId()));
            return true;
        }
        if (name == "aura.getCaster")
        {
            if (auto h = liveHandle(aura->GetCaster()))
                pushHandle(out, *h);
            return true;
        }
        if (name == "aura.getCasterGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(aura->GetCasterGUID())));
            return true;
        }
        if (name == "aura.getCasterLevel")
        {
            out.emplace_back(static_cast<int64_t>(aura->GetCasterLevel()));
            return true;
        }
        if (name == "aura.getDuration")
        {
            out.emplace_back(static_cast<int64_t>(aura->GetDuration()));
            return true;
        }
        if (name == "aura.setDuration")
        {
            aura->SetDuration(static_cast<int32>(iArg(args, 0)));
            return true;
        }
        if (name == "aura.getMaxDuration")
        {
            out.emplace_back(static_cast<int64_t>(aura->GetMaxDuration()));
            return true;
        }
        if (name == "aura.setMaxDuration")
        {
            aura->SetMaxDuration(static_cast<int32>(iArg(args, 0)));
            return true;
        }
        if (name == "aura.getStackAmount")
        {
            out.emplace_back(static_cast<int64_t>(aura->GetStackAmount()));
            return true;
        }
        if (name == "aura.setStackAmount")
        {
            uint32 const amount = static_cast<uint32>(iArg(args, 0));
            // The core's own stack change (SpellAuras.cpp:1097-1112) removes an aura whose count reaches zero and
            // clamps an increase to CalcMaxStackAmount (a non-stackable aura becomes one stack, SpellAuras.cpp:1104).
            // The direct setter SpellAuras.cpp:1061 does neither, so the host applies the same rules before it.
            if (amount == 0)
            {
                aura->Remove();
                return true;
            }
            uint32 const maxAmount = std::max<uint32>(aura->CalcMaxStackAmount(), 1);
            uint8 const newAmount = static_cast<uint8>(std::min<uint32>(amount, maxAmount));
            uint8 const old = aura->GetStackAmount();
            aura->SetStackAmount(newAmount);
            // Docs: when the new count is not lower than the old one, the aura's duration resets to its maximum. The
            // modded path of RefreshDuration (withMods true) would also rewrite the max from the spell's base data and
            // haste, dropping the max the aura already carries; the docs only reset the duration, so no mods.
            if (newAmount >= old)
                aura->RefreshDuration(false);
            return true;
        }
        if (name == "aura.getOwner")
        {
            // Docs: the Unit the aura is applied to (a dynobj aura has no unit owner and answers nil).
            WorldObject* owner = aura->GetOwner();
            if (owner && owner->IsUnit())
                if (auto h = liveHandle(owner))
                    pushHandle(out, *h);
            return true;
        }
        if (name == "aura.getSpellInfo")
        {
            if (SpellInfo const* info = aura->GetSpellInfo())
            {
                fl::HandleData h;
                h.kind = ObjKind::SpellInfo;
                h.guid.lo = info->Id;
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "aura.remove")
        {
            aura->Remove();
            return true;
        }
        return false;
    }
    WorldObject* obj = woOf(self);
    if (!obj)
        return false;

    if (name == "object.getScale")
    {
        out.emplace_back(static_cast<double>(obj->GetObjectScale()));
        return true;
    }
    if (name == "object.setScale")
    {
        obj->SetObjectScale(static_cast<float>(dArg(args, 0, 1.0)));
        return true;
    }
    if (name == "worldobject.getName")
    {
        out.emplace_back(obj->GetName());
        return true;
    }
    if (name == "worldobject.getZoneId")
    {
        out.emplace_back(static_cast<int64_t>(obj->GetZoneId()));
        return true;
    }
    if (name == "worldobject.getAreaId")
    {
        out.emplace_back(static_cast<int64_t>(obj->GetAreaId()));
        return true;
    }
    if (name == "worldobject.getX")
    {
        out.emplace_back(static_cast<double>(obj->GetPositionX()));
        return true;
    }
    if (name == "worldobject.getY")
    {
        out.emplace_back(static_cast<double>(obj->GetPositionY()));
        return true;
    }
    if (name == "worldobject.getZ")
    {
        out.emplace_back(static_cast<double>(obj->GetPositionZ()));
        return true;
    }
    if (name == "worldobject.getO")
    {
        out.emplace_back(static_cast<double>(obj->GetOrientation()));
        return true;
    }
    if (name == "worldobject.getLocation")
    {
        out.emplace_back(static_cast<double>(obj->GetPositionX()));
        out.emplace_back(static_cast<double>(obj->GetPositionY()));
        out.emplace_back(static_cast<double>(obj->GetPositionZ()));
        out.emplace_back(static_cast<double>(obj->GetOrientation()));
        return true;
    }
    if (name == "worldobject.getDistance")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(static_cast<double>(obj->GetDistance(o)));
                return true;
            }
        out.emplace_back(static_cast<double>(obj->GetDistance(static_cast<float>(dArg(args, 0)),
                                                              static_cast<float>(dArg(args, 1)),
                                                              static_cast<float>(dArg(args, 2)))));
        return true;
    }
    if (name == "worldobject.getAngle")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(static_cast<double>(obj->GetAbsoluteAngle(o)));
                return true;
            }
        out.emplace_back(static_cast<double>(
            obj->GetAbsoluteAngle(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)))));
        return true;
    }
    if (name == "worldobject.getDistance2d")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(static_cast<double>(obj->GetDistance2d(o)));
                return true;
            }
        out.emplace_back(static_cast<double>(
            obj->GetDistance2d(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)))));
        return true;
    }
    if (name == "worldobject.getExactDistance")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(static_cast<double>(obj->GetExactDist(o)));
                return true;
            }
        out.emplace_back(static_cast<double>(obj->GetExactDist(static_cast<float>(dArg(args, 0)),
                                                               static_cast<float>(dArg(args, 1)),
                                                               static_cast<float>(dArg(args, 2)))));
        return true;
    }
    if (name == "worldobject.getExactDistance2d")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(static_cast<double>(obj->GetExactDist2d(o)));
                return true;
            }
        out.emplace_back(static_cast<double>(
            obj->GetExactDist2d(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)))));
        return true;
    }
    if (name == "worldobject.getRelativePoint")
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        obj->GetClosePoint(x, y, z, 0.0f, static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)));
        out.emplace_back(static_cast<double>(x));
        out.emplace_back(static_cast<double>(y));
        out.emplace_back(static_cast<double>(z));
        return true;
    }
    if (name == "worldobject.isInFront" || name == "worldobject.isInBack")
    {
        HandleData other;
        if (!args.empty() && asHandle(args[0], other))
            if (WorldObject* o = woOf(other))
            {
                float const arc = static_cast<float>(dArg(args, 1, 3.14159265358979323846));
                out.emplace_back(name == "worldobject.isInFront" ? obj->isInFront(o, arc) : obj->isInBack(o, arc));
            }
        return true;
    }
    if (name == "worldobject.isInMap")
    {
        HandleData other;
        if (!args.empty() && asHandle(args[0], other))
            if (WorldObject* o = woOf(other))
                out.emplace_back(obj->IsInMap(o));
        return true;
    }
    if (name == "worldobject.isInRange")
    {
        HandleData other;
        if (!args.empty() && asHandle(args[0], other))
            if (WorldObject* o = woOf(other))
                out.emplace_back(obj->IsInRange(o, static_cast<float>(dArg(args, 1)),
                                                static_cast<float>(dArg(args, 2)), bArg(args, 3, true)));
        return true;
    }
    if (name == "worldobject.isInRange2d")
    {
        Position pos(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)));
        out.emplace_back(
            obj->IsInRange2d(&pos, static_cast<float>(dArg(args, 2)), static_cast<float>(dArg(args, 3))));
        return true;
    }
    if (name == "worldobject.isInRange3d")
    {
        Position pos(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)),
                     static_cast<float>(dArg(args, 2)));
        out.emplace_back(
            obj->IsInRange3d(&pos, static_cast<float>(dArg(args, 3)), static_cast<float>(dArg(args, 4))));
        return true;
    }
    if (name == "worldobject.isWithinDist")
    {
        HandleData other;
        if (!args.empty() && asHandle(args[0], other))
            if (WorldObject* o = woOf(other))
                out.emplace_back(obj->IsWithinDist(o, static_cast<float>(dArg(args, 1)), bArg(args, 2, true)));
        return true;
    }
    if (name == "worldobject.isWithinDist2d")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(obj->IsWithinDist2d(o, static_cast<float>(dArg(args, 1))));
                return true;
            }
        out.emplace_back(obj->IsWithinDist2d(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)),
                                             static_cast<float>(dArg(args, 2))));
        return true;
    }
    if (name == "worldobject.isWithinDist3d")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(obj->IsWithinDist3d(o, static_cast<float>(dArg(args, 1))));
                return true;
            }
        out.emplace_back(obj->IsWithinDist3d(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)),
                                             static_cast<float>(dArg(args, 2)), static_cast<float>(dArg(args, 3))));
        return true;
    }
    if (name == "worldobject.isWithinDistInMap")
    {
        HandleData other;
        if (!args.empty() && asHandle(args[0], other))
            if (WorldObject* o = woOf(other))
                out.emplace_back(obj->IsWithinDistInMap(o, static_cast<float>(dArg(args, 1)), bArg(args, 2, true)));
        return true;
    }
    if (name == "worldobject.isWithinLoS")
    {
        if (!args.empty())
            if (HandleData other; asHandle(args[0], other))
            {
                if (WorldObject* o = woOf(other))
                    out.emplace_back(obj->IsWithinLOSInMap(o));
                return true;
            }
        out.emplace_back(obj->IsWithinLOS(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)),
                                          static_cast<float>(dArg(args, 2))));
        return true;
    }
    if (name == "worldobject.getNearObjects" || name == "worldobject.getNearObject")
    {
        if (!obj->GetMap())
            return true;
        float const range = flh::clampSearchRange(dArg(args, 0, 533.333));
        uint32 const typeMask = static_cast<uint32>(iArg(args, 1, 0));
        uint32 const entry = static_cast<uint32>(iArg(args, 2, 0));
        int const hostile = static_cast<int>(iArg(args, 3, 0));
        int const dead = static_cast<int>(iArg(args, 4, 1));
        std::vector<std::pair<float, HandleData>> found;
        auto work = [&](WorldObject* o)
        {
            if (!o || o == obj || !o->IsInWorld())
                return;
            float const dist = obj->GetDistance(o);
            if (dist > range)
                return;
            if (typeMask)
            {
                bool match = false;
                if ((typeMask & (1u << 3)) && o->IsUnit())
                    match = true;
                if ((typeMask & (1u << 4)) && o->IsPlayer())
                    match = true;
                if ((typeMask & (1u << 5)) && o->IsGameObject())
                    match = true;
                if ((typeMask & (1u << 7)) && o->ToCorpse())
                    match = true;
                if (!match)
                    return;
            }
            if (entry && o->GetEntry() != entry)
                return;
            if (hostile == 1 && !obj->IsHostileTo(o))
                return;
            if (hostile == 2 && !obj->IsFriendlyTo(o))
                return;
            if (Unit* u = o->ToUnit())
            {
                if (dead == 1 && !u->IsAlive())
                    return;
                if (dead == 2 && u->IsAlive())
                    return;
            }
            if (auto h = targetHandle(o))
                found.emplace_back(dist, *h);
        };
        Trinity::WorldObjectWorker worker(obj, work);
        flh::visitAllNearObjects<::Cell>(obj, worker, range);
        std::sort(found.begin(), found.end(),
                  [](auto const& a, auto const& b) { return a.first < b.first; });
        if (name == "worldobject.getNearObject")
        {
            if (!found.empty())
                pushHandle(out, found.front().second);
        }
        else
        {
            for (auto const& item : found)
                pushHandle(out, item.second);
        }
        return true;
    }
    if (name == "worldobject.playDirectSound" || name == "worldobject.playDistanceSound" ||
        name == "worldobject.playMusic")
    {
        uint32 const id = static_cast<uint32>(iArg(args, 0));
        Player* target = nullptr;
        if (args.size() > 1)
            if (HandleData other; asHandle(args[1], other))
                target = playerOf(other);
        if (name == "worldobject.playDirectSound")
            obj->PlayDirectSound(id, target);
        else if (name == "worldobject.playDistanceSound")
            obj->PlayDistanceSound(id, target);
        else
            obj->PlayDirectMusic(id, target);
        return true;
    }
    if (name == "worldobject.say" || name == "worldobject.yell")
    {
        std::string const msg = sArg(args, 0);
        auto const lang = static_cast<Language>(iArg(args, 1));
        bool const isSay = (name == "worldobject.say");
        if (Unit* u = obj->ToUnit())
        {
            if (isSay)
                u->Say(msg, lang);
            else
                u->Yell(msg, lang);
        }
        else if (obj->GetMap())
        {
            float const range = sWorld->getFloatConfig(isSay ? CONFIG_LISTEN_RANGE_SAY : CONFIG_LISTEN_RANGE_YELL);
            Trinity::CustomChatTextBuilder builder(obj, isSay ? CHAT_MSG_MONSTER_SAY : CHAT_MSG_MONSTER_YELL, msg, lang);
            Trinity::LocalizedDo<Trinity::CustomChatTextBuilder> localizer(builder);
            Trinity::PlayerDistWorker<Trinity::LocalizedDo<Trinity::CustomChatTextBuilder>> worker(obj, range,
                                                                                                   localizer);
            ::Cell::VisitWorldObjects(obj, worker, range);
        }
        return true;
    }
    if (name == "worldobject.whisper")
    {
        std::string const msg = sArg(args, 0);
        auto const lang = static_cast<Language>(iArg(args, 1));
        bool const boss = bArg(args, 3, false);
        HandleData receiver;
        if (args.size() > 2 && asHandle(args[2], receiver))
        {
            if (Player* p = playerOf(receiver))
            {
                if (Unit* u = obj->ToUnit())
                    u->Whisper(msg, lang, p, boss);
                else if (p->GetSession())
                {
                    LocaleConstant const locale = p->GetSession()->GetSessionDbLocaleIndex();
                    WorldPackets::Chat::Chat packet;
                    packet.Initialize(boss ? CHAT_MSG_RAID_BOSS_WHISPER : CHAT_MSG_MONSTER_WHISPER, lang, obj, p, msg,
                                      0, "", locale);
                    p->SendDirectMessage(packet.Write());
                }
            }
        }
        return true;
    }
    if (name == "worldobject.spawnCreature")
    {
        Map* m = obj->GetMap();
        if (!m)
            return false;
        TempSummon* c = m->SummonCreature(static_cast<uint32>(iArg(args, 0)),
                                          Position(static_cast<float>(dArg(args, 1)), static_cast<float>(dArg(args, 2)),
                                                   static_cast<float>(dArg(args, 3)), static_cast<float>(dArg(args, 4))),
                                          nullptr, Milliseconds(static_cast<uint32>(iArg(args, 6))), obj);
        if (!c)
            return true;
        pushHandle(out, handleFor(c));
        return true;
    }
    if (name == "worldobject.summonGameObject")
    {
        float const x = static_cast<float>(dArg(args, 1));
        float const y = static_cast<float>(dArg(args, 2));
        float const z = static_cast<float>(dArg(args, 3));
        float const o = static_cast<float>(dArg(args, 4));
        GameObject* go = obj->SummonGameObject(static_cast<uint32>(iArg(args, 0)), x, y, z, o, QuaternionData(),
                                               Seconds(static_cast<uint32>(iArg(args, 5, 30))));
        if (go)
            pushHandle(out, handleFor(go));
        return true;
    }

    // ---- Unit ----
    Unit* unit = unitOf(self);
    if (unit)
    {
        if (name == "unit.getLevel")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetLevel()));
            return true;
        }
        if (name == "unit.setLevel")
        {
            unit->SetLevel(static_cast<uint8>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.getHealth")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetHealth()));
            return true;
        }
        if (name == "unit.setHealth")
        {
            unit->SetHealth(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.getMaxHealth")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetMaxHealth()));
            return true;
        }
        if (name == "unit.getHealthPct")
        {
            out.emplace_back(static_cast<double>(unit->GetHealthPct()));
            return true;
        }
        if (name == "unit.getPower")
        {
            Powers power = unit->GetPowerType();
            if (int64_t const p = iArg(args, 0, -1); p >= 0 && p < MAX_POWERS)
                power = static_cast<Powers>(p);
            out.emplace_back(static_cast<int64_t>(unit->GetPower(power)));
            return true;
        }
        if (name == "unit.setPower")
        {
            Powers power = unit->GetPowerType();
            if (int64_t const p = iArg(args, 1, -1); p >= 0 && p < MAX_POWERS)
                power = static_cast<Powers>(p);
            // the amount is read as 0..4294967295; as an int32 the upper half would wrap to a negative power
            unit->SetPower(power, static_cast<int32>(std::min<int64_t>(iArg(args, 0), std::numeric_limits<int32>::max())));
            return true;
        }
        if (name == "unit.isAlive")
        {
            out.emplace_back(unit->IsAlive());
            return true;
        }
        if (name == "unit.isDead")
        {
            out.emplace_back(unit->isDead());
            return true;
        }
        if (name == "unit.isInCombat")
        {
            out.emplace_back(unit->IsInCombat());
            return true;
        }
        if (name == "unit.getVictim")
        {
            if (Unit* v = unit->GetVictim())
                pushHandle(out, handleFor(v));
            return true;
        }
        if (name == "unit.attack")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* v = unitOf(other))
                {
                    unit->Attack(v, bArg(args, 1, false));
                    out.emplace_back(true);
                }
            return true;
        }
        if (name == "unit.attackStop")
        {
            bool const was = unit->GetVictim() != nullptr;
            unit->AttackStop();
            out.emplace_back(was);
            return true;
        }
        if (name == "unit.castSpell")
        {
            HandleData target;
            Unit* t = nullptr;
            if (!args.empty() && asHandle(args[0], target))
                t = unitOf(target);
            uint32 const spell = static_cast<uint32>(iArg(args, 1));
            unit->CastSpell(t, spell, CastSpellExtraArgs(bArg(args, 2, false)));
            return true;
        }
        if (name == "unit.castCustomSpell")
        {
            HandleData target;
            Unit* t = nullptr;
            if (!args.empty() && asHandle(args[0], target))
                t = unitOf(target);
            uint32 const spell = static_cast<uint32>(iArg(args, 1));
            CastSpellExtraArgs extra(bArg(args, 2, false));
            for (size_t i = 3; i < 6 && i < args.size(); ++i)
                if (args[i].v.index() != 0)
                    extra.AddSpellMod(SpellValueMod(SPELLVALUE_BASE_POINT0 + int(i - 3)), static_cast<int32>(iArg(args, i)));
            HandleData item;
            if (args.size() > 6 && asHandle(args[6], item))
                extra.SetCastItem(findItem(item));
            int64_t const originalCaster = iArg(args, 7);
            if (originalCaster)
                extra.SetOriginalCaster(ObjectGuid::Create<HighGuid::Player>(static_cast<uint64>(originalCaster)));
            unit->CastSpell(t, spell, extra);
            return true;
        }
        if (name == "unit.addAura")
        {
            uint32 const spell = static_cast<uint32>(iArg(args, 0));
            Unit* target = unit;
            if (args.size() > 1)
                if (HandleData h; asHandle(args[1], h))
                    target = unitOf(h);
            if (target)
                if (Aura* aura = unit->AddAura(spell, target))
                {
                    HandleData h;
                    h.kind = ObjKind::Aura;
                    h.guid = fromGuid(aura->GetCasterGUID());
                    h.mapId = spell;
                    h.instanceId = rememberAura(aura);
                    pushHandle(out, h);
                }
            return true;   // the transient Aura handle of E2
        }
        if (name == "unit.removeAura")
        {
            unit->RemoveAura(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.hasAura")
        {
            out.emplace_back(unit->HasAura(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "unit.kill")
        {
            HandleData target;
            if (!args.empty() && asHandle(args[0], target))
                if (Unit* t = unitOf(target))
                    Unit::Kill(unit, t, bArg(args, 1, true));
            return true;
        }
        if (name == "unit.dealDamage")
        {
            HandleData target;
            if (!args.empty() && asHandle(args[0], target))
                if (Unit* t = unitOf(target))
                {
                    uint32 const damage = static_cast<uint32>(iArg(args, 1));
                    SpellInfo const* spell = nullptr;
                    if (uint32 const id = static_cast<uint32>(iArg(args, 4)); id)
                        spell = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
                    Unit::DealDamage(unit, t, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, spell,
                                     bArg(args, 2, true));
                }
            return true;
        }
        if (name == "unit.moveTo")
        {
            uint32 const id = static_cast<uint32>(iArg(args, 0));
            unit->GetMotionMaster()->MovePoint(id, static_cast<float>(dArg(args, 1)), static_cast<float>(dArg(args, 2)),
                                               static_cast<float>(dArg(args, 3)), bArg(args, 4, true));
            return true;
        }
        if (name == "unit.moveFollow")
        {
            HandleData target;
            if (!args.empty() && asHandle(args[0], target))
                if (Unit* t = unitOf(target))
                    unit->GetMotionMaster()->MoveFollow(t, static_cast<float>(dArg(args, 1)),
                                                        ChaseAngle(static_cast<float>(dArg(args, 2))));
            return true;
        }
        if (name == "unit.moveStop")
        {
            unit->StopMoving();
            return true;
        }
        if (name == "unit.moveHome")
        {
            unit->GetMotionMaster()->MoveTargetedHome();
            return true;
        }
        if (name == "unit.setFaction")
        {
            unit->SetFaction(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.getFaction")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetFaction()));
            return true;
        }
        if (name == "unit.isHostileTo")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* o = unitOf(other))
                    out.emplace_back(unit->IsHostileTo(o));
            return true;
        }
        if (name == "unit.isFriendlyTo")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* o = unitOf(other))
                    out.emplace_back(unit->IsFriendlyTo(o));
            return true;
        }
        if (name == "unit.setDisplayId")
        {
            unit->SetDisplayId(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.emote")
        {
            unit->HandleEmoteCommand(static_cast<Emote>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.getOwner")
        {
            if (Unit* owner = unit->GetOwner())
                pushHandle(out, handleFor(owner));
            return true;
        }
        if (name == "unit.getClass")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetClass()));
            return true;
        }
        if (name == "unit.getRace")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetRace()));
            return true;
        }
        if (name == "unit.getGender")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetGender()));
            return true;
        }
        if (name == "unit.setPvP")
        {
            unit->SetPvP(bArg(args, 0, true));
            return true;
        }
        if (name == "unit.say" || name == "unit.yell")
        {
            std::string const msg = sArg(args, 0);
            auto const lang = static_cast<Language>(iArg(args, 1));
            if (name == "unit.say")
                unit->Say(msg, lang);
            else
                unit->Yell(msg, lang);
            return true;
        }
        if (name == "unit.whisper")
        {
            std::string const msg = sArg(args, 0);
            auto const lang = static_cast<Language>(iArg(args, 1));
            HandleData receiver;
            if (args.size() > 2 && asHandle(args[2], receiver))
                if (Player* p = playerOf(receiver))
                    unit->Whisper(msg, lang, p, bArg(args, 3, false));
            return true;
        }

        // ---- E5 long tail, round 1: the most used documented Unit methods (docs Unit/index.html) ----
        if (name == "unit.getMaxPower")
        {
            Powers power = unit->GetPowerType();
            if (int64_t const p = iArg(args, 0, -1); p >= 0 && p < MAX_POWERS)
                power = static_cast<Powers>(p);
            out.emplace_back(static_cast<int64_t>(unit->GetMaxPower(power)));
            return true;
        }
        if (name == "unit.setMaxPower")
        {
            Powers power = unit->GetPowerType();
            if (int64_t const p = iArg(args, 0, -1); p >= 0 && p < MAX_POWERS)
                power = static_cast<Powers>(p);
            unit->SetMaxPower(power, static_cast<int32>(std::min<int64_t>(iArg(args, 1), std::numeric_limits<int32>::max())));
            return true;
        }
        if (name == "unit.getPowerType")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetPowerType()));
            return true;
        }
        if (name == "unit.setPowerType")
        {
            int64_t const p = iArg(args, 0);
            if (p >= 0 && p < MAX_POWERS)
                unit->SetPowerType(static_cast<Powers>(p));
            return true;
        }
        if (name == "unit.getPowerPct")
        {
            Powers power = unit->GetPowerType();
            if (int64_t const p = iArg(args, 0, -1); p >= 0 && p < MAX_POWERS)
                power = static_cast<Powers>(p);
            out.emplace_back(static_cast<double>(unit->GetPowerPct(power)));
            return true;
        }
        if (name == "unit.setMaxHealth")
        {
            unit->SetMaxHealth(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.getStat")
        {
            int64_t const s = iArg(args, 0);
            if (s >= 0 && s < MAX_STATS)
                out.emplace_back(static_cast<double>(unit->GetStat(static_cast<Stats>(s))));
            return true;
        }
        if (name == "unit.getBaseSpellPower")
        {
            int64_t const s = iArg(args, 0);
            if (s >= 0 && s < MAX_SPELL_SCHOOL)
                out.emplace_back(static_cast<int64_t>(
                    std::max(0, unit->SpellBaseDamageBonusDone(SpellSchoolMask(1u << s)))));
            return true;
        }
        if (name == "unit.modifyPower")
        {
            Powers power = unit->GetPowerType();
            if (int64_t const p = iArg(args, 1, -1); p >= 0 && p < MAX_POWERS)
                power = static_cast<Powers>(p);
            unit->ModifyPower(power, static_cast<int32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.isStandState")
        {
            out.emplace_back(unit->IsStandState());
            return true;
        }
        if (name == "unit.setStandState")
        {
            if (int64_t const s = iArg(args, 0); s >= 0 && s < MAX_UNIT_STAND_STATE)
                unit->SetStandState(static_cast<UnitStandStateType>(s));
            return true;
        }
        if (name == "unit.isMounted")
        {
            out.emplace_back(unit->IsMounted());
            return true;
        }
        if (name == "unit.mount")
        {
            unit->Mount(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.dismount")
        {
            unit->Dismount();
            return true;
        }
        if (name == "unit.getMountId")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetMountDisplayId()));
            return true;
        }
        if (name == "unit.isStopped")
        {
            out.emplace_back(unit->IsStopped());
            return true;
        }
        if (name == "unit.isCasting")
        {
            out.emplace_back(unit->IsNonMeleeSpellCast(false));
            return true;
        }
        if (name == "unit.isFullHealth")
        {
            out.emplace_back(unit->IsFullHealth());
            return true;
        }
        if (name == "unit.isRooted")
        {
            out.emplace_back(unit->HasUnitState(UNIT_STATE_ROOT));
            return true;
        }
        if (name == "unit.hasUnitState")
        {
            out.emplace_back(unit->HasUnitState(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "unit.addUnitState")
        {
            unit->AddUnitState(static_cast<uint32>(iArg(args, 0)) & UNIT_STATE_ALL_STATE_SUPPORTED);
            return true;
        }
        if (name == "unit.clearUnitState")
        {
            unit->ClearUnitState(static_cast<uint32>(iArg(args, 0)) & UNIT_STATE_ALL_STATE_SUPPORTED);
            return true;
        }
        if (name == "unit.isInWater")
        {
            out.emplace_back(unit->IsInWater());
            return true;
        }
        if (name == "unit.isUnderWater")
        {
            out.emplace_back(unit->IsUnderWater());
            return true;
        }
        if (name == "unit.isPvPFlagged")
        {
            out.emplace_back(unit->IsPvP());
            return true;
        }
        if (name == "unit.isAttackingPlayer")
        {
            Unit* const victim = unit->GetVictim();
            out.emplace_back(victim && victim->GetTypeId() == TYPEID_PLAYER);
            return true;
        }
        if (name == "unit.isCharmed")
        {
            out.emplace_back(unit->IsCharmed());
            return true;
        }
        if (name == "unit.isVendor") { out.emplace_back(unit->IsVendor()); return true; }
        if (name == "unit.isQuestGiver") { out.emplace_back(unit->IsQuestGiver()); return true; }
        if (name == "unit.isTrainer") { out.emplace_back(unit->IsTrainer()); return true; }
        if (name == "unit.isBanker") { out.emplace_back(unit->IsBanker()); return true; }
        if (name == "unit.isInnkeeper") { out.emplace_back(unit->IsInnkeeper()); return true; }
        if (name == "unit.isGossip") { out.emplace_back(unit->IsGossip()); return true; }
        if (name == "unit.isSpiritHealer") { out.emplace_back(unit->IsSpiritHealer()); return true; }
        if (name == "unit.isServiceProvider") { out.emplace_back(unit->IsServiceProvider()); return true; }
        if (name == "unit.getSpeed")
        {
            int64_t const t = iArg(args, 0);
            if (t >= 0 && t < MAX_MOVE_TYPE)
                // the rate SetSpeed writes (1.0 = the base speed), so a read gives back what a write set
                out.emplace_back(static_cast<double>(unit->GetSpeedRate(static_cast<UnitMoveType>(t))));
            return true;
        }
        if (name == "unit.setSpeed")
        {
            int64_t const t = iArg(args, 0);
            if (t >= 0 && t < MAX_MOVE_TYPE)
            {
                // SetSpeedRate already sends the speed packet to the client on a change; UpdateSpeed would recompute
                // the speed from auras and the template and put the old rate straight back, so `forced` adds nothing.
                unit->SetSpeedRate(static_cast<UnitMoveType>(t),
                                   std::clamp(static_cast<float>(dArg(args, 1)), 0.01f, 255.0f));
            }
            return true;
        }
        if (name == "unit.setRooted" || name == "unit.setConfused" || name == "unit.setFeared" ||
            name == "unit.setStunned")
        {
            uint32 const control = name == "unit.setRooted"     ? UNIT_STATE_ROOT
                                   : name == "unit.setConfused" ? UNIT_STATE_CONFUSED
                                   : name == "unit.setFeared"   ? UNIT_STATE_FLEEING
                                                                : UNIT_STATE_STUNNED;
            unit->SetControlled(bArg(args, 0, true), static_cast<UnitState>(control));
            return true;
        }
        if (name == "unit.moveRandom")
        {
            // the active slot: a creature's default slot is its own home wandering, which this would overwrite and
            // MoveClear would not remove
            unit->GetMotionMaster()->MoveRandom(std::max(0.0f, static_cast<float>(dArg(args, 0))), {}, {},
                                                MovementWalkRunSpeedSelectionMode::ForceWalk, MOTION_SLOT_ACTIVE);
            return true;
        }
        if (name == "unit.moveChase")
        {
            HandleData target;
            if (!args.empty() && asHandle(args[0], target))
                if (Unit* t = unitOf(target))
                {
                    // an absent (or zero) distance and an absent angle keep the core's own chase: melee range, any side
                    Optional<ChaseRange> range;
                    Optional<ChaseAngle> angle;
                    double d = 0, a = 0;
                    if (args.size() > 1 && asDouble(args[1], d) && d > 0)
                        range = ChaseRange(static_cast<float>(d));
                    if (args.size() > 2 && asDouble(args[2], a))
                        angle = ChaseAngle(static_cast<float>(a));
                    unit->GetMotionMaster()->MoveChase(t, range, angle);
                }
            return true;
        }
        if (name == "unit.moveConfused")
        {
            unit->GetMotionMaster()->MoveConfused();
            return true;
        }
        if (name == "unit.moveFleeing")
        {
            HandleData target;
            if (!args.empty() && asHandle(args[0], target))
                if (Unit* t = unitOf(target))
                    unit->GetMotionMaster()->MoveFleeing(t, Milliseconds(iArg(args, 1, 0)));
            return true;
        }
        if (name == "unit.moveIdle")
        {
            // MoveIdle only replaces the default slot; the active generators (MoveTo, MoveChase, MoveFollow) would keep
            // running, so they go first
            unit->GetMotionMaster()->Clear(MOTION_SLOT_ACTIVE);
            unit->GetMotionMaster()->MoveIdle();
            return true;
        }
        if (name == "unit.moveJump")
        {
            // the core clamps the arc height between a minimum (0.5 by default) and the maximum, and std::clamp with
            // lo > hi is undefined: a maximum below 0.5 therefore also lowers the minimum
            float const maxHeight = std::max(0.0f, static_cast<float>(dArg(args, 4)));
            unit->GetMotionMaster()->MoveJump(static_cast<uint32>(iArg(args, 5, 0)),
                                              Position(static_cast<float>(dArg(args, 0)),
                                                       static_cast<float>(dArg(args, 1)),
                                                       static_cast<float>(dArg(args, 2))),
                                              static_cast<float>(dArg(args, 3)), std::min(0.5f, maxHeight), maxHeight);
            return true;
        }
        if (name == "unit.moveExpire")
        {
            // docs: "Cleans movement": drops the active-slot generators; `reset` re-initializes the default movement.
            unit->GetMotionMaster()->Clear(MOTION_SLOT_ACTIVE);
            if (bArg(args, 0, true))
                unit->GetMotionMaster()->InitializeDefault();
            return true;
        }
        if (name == "unit.moveClear")
        {
            // docs: "Clean movement": the same clearing; `reset` re-initializes the default movement.
            unit->GetMotionMaster()->Clear(MOTION_SLOT_ACTIVE);
            if (bArg(args, 0, true))
                unit->GetMotionMaster()->InitializeDefault();
            return true;
        }
        if (name == "unit.nearTeleport")
        {
            unit->NearTeleportTo(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)),
                                 static_cast<float>(dArg(args, 2)), static_cast<float>(dArg(args, 3)));
            return true;
        }
        if (name == "unit.getMovementType")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetMotionMaster()->GetCurrentMovementGeneratorType()));
            return true;
        }
        if (name == "unit.setFacing")
        {
            unit->SetFacingTo(static_cast<float>(dArg(args, 0)));
            return true;
        }
        if (name == "unit.setFacingToObject")
        {
            HandleData target;
            if (!args.empty() && asHandle(args[0], target))
                if (WorldObject* o = woOf(target))
                    unit->SetFacingToObject(o);
            return true;
        }
        if (name == "unit.healthBelowPct")
        {
            out.emplace_back(unit->HealthBelowPct(static_cast<float>(dArg(args, 0))));
            return true;
        }
        if (name == "unit.healthAbovePct")
        {
            out.emplace_back(unit->HealthAbovePct(static_cast<float>(dArg(args, 0))));
            return true;
        }
        if (name == "unit.clearInCombat")
        {
            unit->ClearInCombat();
            return true;
        }
        if (name == "unit.setInCombatWith")
        {
            HandleData enemy;
            if (!args.empty() && asHandle(args[0], enemy))
                if (Unit* e = unitOf(enemy))
                    unit->SetInCombatWith(e);
            return true;
        }
        if (name == "unit.dealHeal")
        {
            HandleData target;
            Unit* t = nullptr;
            if (!args.empty() && asHandle(args[0], target))
                t = unitOf(target);
            uint32 const spell = static_cast<uint32>(iArg(args, 1));
            if (t)
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE))
                {
                    HealInfo heal(unit, t, static_cast<uint32>(iArg(args, 2)), info, SPELL_SCHOOL_MASK_NORMAL);
                    unit->HealBySpell(heal, bArg(args, 3, false));
                }
            return true;
        }
        if (name == "unit.stopSpellCast")
        {
            unit->InterruptNonMeleeSpells(false, static_cast<uint32>(iArg(args, 0, 0)));
            return true;
        }
        if (name == "unit.interruptSpell")
        {
            int64_t const t = iArg(args, 0);
            if (t >= 0 && t < CURRENT_MAX_SPELL)
                unit->InterruptSpell(static_cast<CurrentSpellTypes>(t), bArg(args, 1, true));
            return true;
        }
        if (name == "unit.getCurrentSpell")
        {
            int64_t const t = iArg(args, 0);
            if (t >= 0 && t < CURRENT_MAX_SPELL)
                if (Spell* spell = unit->GetCurrentSpell(static_cast<CurrentSpellTypes>(t)))
                    pushHandle(out, handleForSpell(spell));
            return true;
        }
        if (name == "unit.getAura" || name == "unit.getOwnedAura")
        {
            uint32 const spellId = static_cast<uint32>(iArg(args, 0));
            ObjectGuid const caster = guidArgAt(args, 1, LegacyGuid::Player);
            ObjectGuid const itemCaster = guidArgAt(args, 2, LegacyGuid::Item);
            uint32 const reqEffMask = static_cast<uint32>(iArg(args, 3, 0));
            Aura* aura = nullptr;
            if (name == "unit.getAura")
                aura = unit->GetAura(spellId, caster, itemCaster, reqEffMask);
            else
            {
                Aura* except = nullptr;
                HandleData h;
                if (args.size() > 4 && asHandle(args[4], h) && h.kind == ObjKind::Aura)
                    except = auraById(h.instanceId);
                aura = unit->GetOwnedAura(spellId, caster, itemCaster, reqEffMask, except);
            }
            if (aura)
            {
                HandleData h;
                h.kind = ObjKind::Aura;
                h.guid = fromGuid(aura->GetCasterGUID());
                h.mapId = spellId;
                h.instanceId = rememberAura(aura);
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "unit.removeAllAuras")
        {
            unit->RemoveAllAuras();
            return true;
        }
        if (name == "unit.removeArenaAuras")
        {
            unit->RemoveArenaAuras();
            return true;
        }
        if (name == "unit.getCharmerGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetCharmerGUID())));
            return true;
        }
        if (name == "unit.getOwnerGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetOwnerGUID())));
            return true;
        }
        if (name == "unit.getControllerGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetCharmerOrOwnerGUID())));
            return true;
        }
        if (name == "unit.getCreatorGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetCreatorGUID())));
            return true;
        }
        if (name == "unit.getPetGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetPetGUID())));
            return true;
        }
        if (name == "unit.setOwnerGuid")
        {
            unit->SetOwnerGUID(guidArgAt(args, 0, LegacyGuid::Player));
            return true;
        }
        if (name == "unit.getFriendlyUnitsInRange" || name == "unit.getUnfriendlyUnitsInRange")
        {
            float range = static_cast<float>(dArg(args, 0, 533.333));
            if (!std::isfinite(range))
                range = 533.333f;
            range = std::clamp(range, 0.0f, 533.333f);   // one grid; a larger radius only widens the cell walk
            std::list<Unit*> units;
            if (name == "unit.getFriendlyUnitsInRange")
            {
                Trinity::AnyFriendlyUnitInObjectRangeCheck check(unit, unit, range);
                Trinity::UnitListSearcher<Trinity::AnyFriendlyUnitInObjectRangeCheck> searcher(unit, units, check);
                ::Cell::VisitAllObjects(unit, searcher, range);
            }
            else
            {
                Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(unit, unit, range);
                Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(unit, units, check);
                ::Cell::VisitAllObjects(unit, searcher, range);
            }
            for (Unit* u : units)
                if (u->IsInWorld())
                    pushHandle(out, handleFor(u));
            return true;
        }
        if (name == "unit.getDisplayId")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetDisplayId()));
            return true;
        }
        if (name == "unit.getNativeDisplayId")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetNativeDisplayId()));
            return true;
        }
        if (name == "unit.setNativeDisplayId")
        {
            // Unit::GetCollisionHeight asserts the native display is in both tables: an unknown id would abort the
            // server at the next mount, LOS or scale step, so it is refused here (false -> the binding raises)
            uint32 const displayId = static_cast<uint32>(iArg(args, 0));
            CreatureDisplayInfoEntry const* info = sCreatureDisplayInfoStore.LookupEntry(displayId);
            if (!info || !sCreatureModelDataStore.LookupEntry(info->ModelID))
            {
                out.emplace_back(false);
                return true;
            }
            unit->SetDisplayId(displayId, true);
            return true;
        }
        if (name == "unit.getCreatureType")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetCreatureType()));
            return true;
        }
        if (name == "unit.getClassAsString")
        {
            char const* n = sDB2Manager.GetChrClassName(unit->GetClass(), localeArgAt(args, 0));
            if (n && *n)
                out.emplace_back(std::string(n));
            return true;
        }
        if (name == "unit.getRaceAsString")
        {
            char const* n = sDB2Manager.GetChrRaceName(unit->GetRace(), localeArgAt(args, 0));
            if (n && *n)
                out.emplace_back(std::string(n));
            return true;
        }
        if (name == "unit.getClassMask")
        {
            // Unit::GetClassMask shifts by GetClass() - 1: a unit with class 0 (or past 32) is undefined behaviour there
            uint8 const c = unit->GetClass();
            out.emplace_back(static_cast<int64_t>(c >= 1 && c <= 32 ? 1u << (c - 1) : 0u));
            return true;
        }
        if (name == "unit.getRaceMask")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetRaceMask()));
            return true;
        }
        if (name == "unit.sendChatMessageToPlayer")
        {
            HandleData target;
            Player* p = nullptr;
            if (args.size() > 3 && asHandle(args[3], target))
                p = playerOf(target);
            if (p)
            {
                // To this player only, with the type the script gave: Unit::Talk would reach every player in range
                // (and miss this one when out of range) and Unit::Whisper turns the type into a monster whisper.
                std::string const msg = sArg(args, 2);
                auto const lang = static_cast<Language>(iArg(args, 1));
                auto const msgType = static_cast<ChatMsg>(iArg(args, 0));
                WorldPackets::Chat::Chat packet;
                packet.Initialize(msgType, lang, unit, p, msg, 0, "", p->GetSession()->GetSessionDbLocaleIndex());
                p->SendDirectMessage(packet.Write());
            }
            return true;
        }
        if (name == "unit.performEmote")
        {
            unit->HandleEmoteCommand(static_cast<Emote>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.emoteState")
        {
            unit->SetEmoteState(static_cast<Emote>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.isMoving")
        {
            // the movement-generator states cover a creature walking a path; a player's client-driven movement and a
            // jump show only in the movement flags
            out.emplace_back(unit->HasUnitState(UNIT_STATE_MOVING | UNIT_STATE_JUMPING) || unit->isMoving());
            return true;
        }
        if (name == "unit.isFlying")
        {
            out.emplace_back(unit->IsFlying());
            return true;
        }
        if (name == "unit.isVisible")
        {
            out.emplace_back(unit->IsVisible());
            return true;
        }
        if (name == "unit.setSheath")
        {
            unit->SetSheath(static_cast<SheathState>(iArg(args, 0)));
            return true;
        }
        if (name == "unit.setWaterWalk")
        {
            unit->SetWaterWalking(bArg(args, 0, true));
            return true;
        }
        if (name == "unit.setSanctuary")
        {
            if (bArg(args, 0, true))
                unit->SetPvpFlag(UNIT_BYTE2_FLAG_SANCTUARY);
            else
                unit->RemovePvpFlag(UNIT_BYTE2_FLAG_SANCTUARY);
            return true;
        }
        if (name == "unit.deMorph")
        {
            unit->DeMorph();
            return true;
        }
        if (name == "unit.restoreDisplayId")
        {
            unit->RestoreDisplayId();
            return true;
        }
        if (name == "unit.restoreFaction")
        {
            unit->RestoreFaction();
            return true;
        }
        if (name == "unit.addFlatStatModifier")
        {
            uint32 const mod = static_cast<uint32>(iArg(args, 0));
            uint32 const modType = static_cast<uint32>(iArg(args, 1));
            if (mod < UNIT_MOD_END && modType < MODIFIER_TYPE_FLAT_END)
                unit->HandleStatFlatModifier(static_cast<UnitMods>(mod), static_cast<UnitModifierFlatType>(modType),
                                             static_cast<float>(dArg(args, 2)), bArg(args, 3, true));
            return true;
        }
        if (name == "unit.addPctStatModifier")
        {
            uint32 const mod = static_cast<uint32>(iArg(args, 0));
            uint32 const modType = static_cast<uint32>(iArg(args, 1));
            if (mod < UNIT_MOD_END && modType < MODIFIER_TYPE_PCT_END)
                unit->ApplyStatPctModifier(static_cast<UnitMods>(mod), static_cast<UnitModifierPctType>(modType),
                                           static_cast<float>(dArg(args, 2)));
            return true;
        }
        if (name == "unit.canModifyStats")
        {
            out.emplace_back(unit->CanModifyStats());
            return true;
        }
        if (name == "unit.castSpellAoF")
        {
            unit->CastSpell(Position(static_cast<float>(dArg(args, 0)), static_cast<float>(dArg(args, 1)),
                                     static_cast<float>(dArg(args, 2))),
                            static_cast<uint32>(iArg(args, 3)), CastSpellExtraArgs(bArg(args, 4, true)));
            return true;
        }
        if (name == "unit.countPctFromCurHealth")
        {
            out.emplace_back(static_cast<int64_t>(unit->CountPctFromCurHealth(static_cast<float>(dArg(args, 0, 0.0)))));
            return true;
        }
        if (name == "unit.countPctFromMaxHealth")
        {
            out.emplace_back(static_cast<int64_t>(unit->CountPctFromMaxHealth(static_cast<float>(dArg(args, 0, 0.0)))));
            return true;
        }
        if (name == "unit.disableMelee")
        {
            if (Creature* c = unit->ToCreature())
            {
                c->SetCanMelee(!bArg(args, 0, true));
                out.emplace_back(true);
            }
            else
            {
                out.emplace_back(false);
            }
            return true;
        }
        if (name == "unit.getCharmGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetCharmedGUID())));
            return true;
        }
        if (name == "unit.getControllerGuids")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetCharmerOrOwnerOrOwnGUID())));
            return true;
        }
        if (name == "unit.getCritterGuid")
        {
            out.emplace_back(fl::guidText(fromGuid(unit->GetCritterGUID())));
            return true;
        }
        if (name == "unit.getStandState")
        {
            out.emplace_back(static_cast<int64_t>(unit->GetStandState()));
            return true;
        }
        if (name == "unit.isArmorer") { out.emplace_back(unit->IsArmorer()); return true; }
        if (name == "unit.isAuctioneer") { out.emplace_back(unit->IsAuctioner()); return true; }
        if (name == "unit.isBattleMaster") { out.emplace_back(unit->IsBattleMaster()); return true; }
        if (name == "unit.isDying") { out.emplace_back(unit->isDying()); return true; }
        if (name == "unit.isGuildMaster") { out.emplace_back(unit->IsGuildMaster()); return true; }
        if (name == "unit.isInAccessiblePlaceFor")
        {
            bool accessible = false;
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Creature* c = creatureOf(other))
                    accessible = unit->isInAccessiblePlaceFor(c);
            out.emplace_back(accessible);
            return true;
        }
        if (name == "unit.isSpiritGuide") { out.emplace_back(unit->IsAreaSpiritHealer()); return true; }
        if (name == "unit.isSpiritService") { out.emplace_back(unit->IsSpiritService()); return true; }
        if (name == "unit.isTabardDesigner") { out.emplace_back(unit->IsTabardDesigner()); return true; }
        if (name == "unit.isTaxi") { out.emplace_back(unit->IsTaxi()); return true; }
        if (name == "unit.removeBindSightAuras")
        {
            unit->RemoveBindSightAuras();
            return true;
        }
        if (name == "unit.removeCharmAuras")
        {
            unit->RemoveCharmAuras();
            return true;
        }
        if (name == "unit.sendUnitEmote")
        {
            Unit* receiver = nullptr;
            HandleData target;
            if (args.size() > 1 && asHandle(args[1], target))
                receiver = unitOf(target);
            Player* p = unit->ToPlayer();
            if (!p || p->GetSession())
            {
                std::string msg = sArg(args, 0);
                if (msg.size() > 255)
                    msg.resize(255);
                unit->TextEmote(msg, receiver, bArg(args, 2, false));
            }
            return true;
        }
        if (name == "unit.setCanFly")
        {
            unit->SetCanFly(bArg(args, 0, true));
            return true;
        }
        if (name == "unit.setCreatorGuid")
        {
            unit->SetCreatorGUID(guidArgAt(args, 0, LegacyGuid::Player));
            return true;
        }
        if (name == "unit.setCritterGuid")
        {
            unit->SetCritterGUID(guidArgAt(args, 0, LegacyGuid::Creature));
            return true;
        }
        if (name == "unit.setFFA")
        {
            if (bArg(args, 0, true))
                unit->SetPvpFlag(UNIT_BYTE2_FLAG_FFA_PVP);
            else
                unit->RemovePvpFlag(UNIT_BYTE2_FLAG_FFA_PVP);
            return true;
        }
        if (name == "unit.setImmuneTo")
        {
            int64_t const mech = iArg(args, 0);
            if (mech >= 0 && mech < MAX_MECHANIC)
                unit->ApplySpellImmune(0, IMMUNITY_MECHANIC, static_cast<uint32>(mech), bArg(args, 1, true));
            return true;
        }
        if (name == "unit.setName")
        {
            // Player::SaveToDB writes GetName() into characters.name (12 chars), and Pet::SavePetToDB writes m_name into
            // character_pet.name (21 chars), so only non-pet creatures may be renamed.
            Creature* c = unit->ToCreature();
            if (c && !c->IsPet())
            {
                std::string s = sArg(args, 0);
                if (s.size() > 64)
                    s.resize(64);
                c->SetName(std::move(s));
                out.emplace_back(true);
            }
            else
            {
                out.emplace_back(false);
            }
            return true;
        }
        if (name == "unit.setPetGuid")
        {
            unit->SetPetGUID(guidArgAt(args, 0, LegacyGuid::Pet));
            return true;
        }
        if (name == "unit.setVisible")
        {
            unit->SetVisible(bArg(args, 0, true));
            return true;
        }
    }

    // ---- Creature ----
    Creature* creature = creatureOf(self);
    if (creature)
    {
        if (name == "creature.getScriptName")
        {
            out.emplace_back(creature->GetScriptName());
            return true;
        }
        if (name == "creature.isElite")
        {
            out.emplace_back(creature->IsElite());
            return true;
        }
        if (name == "creature.isBoss")
        {
            out.emplace_back(creature->IsDungeonBoss());
            return true;
        }
        if (name == "creature.isWorldBoss")
        {
            out.emplace_back(creature->isWorldBoss());
            return true;
        }
        if (name == "creature.getHomePosition")
        {
            float x = 0, y = 0, z = 0, o = 0;
            creature->GetHomePosition(x, y, z, o);
            out.emplace_back(static_cast<double>(x));
            out.emplace_back(static_cast<double>(y));
            out.emplace_back(static_cast<double>(z));
            out.emplace_back(static_cast<double>(o));
            return true;
        }
        if (name == "creature.respawn")
        {
            creature->Respawn();
            return true;
        }
        if (name == "creature.despawn")
        {
            creature->DespawnOrUnsummon(Milliseconds(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "creature.setReactState")
        {
            creature->SetReactState(static_cast<ReactStates>(iArg(args, 0)));
            return true;
        }
        if (name == "creature.getAITarget")
        {
            if (UnitAI* ai = creature->GetAI())
            {
                auto const method = static_cast<SelectTargetMethod>(iArg(args, 0));
                bool const playerOnly = bArg(args, 1, false);
                uint32 const position = static_cast<uint32>(iArg(args, 2));
                float const distance = static_cast<float>(dArg(args, 3));
                int32 const aura = static_cast<int32>(iArg(args, 4));
                if (Unit* t = ai->SelectTarget(method, position, distance, playerOnly, true, aura))
                    pushHandle(out, handleFor(t));
            }
            return true;
        }
        if (name == "creature.getSpawnId")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetSpawnId()));
            return true;
        }
        if (name == "creature.setRespawnDelay")
        {
            creature->SetRespawnDelay(static_cast<uint32>(iArg(args, 0)));
            return true;
        }

        // ---- long tail round 1: AI and combat control ----
        // A null AI (or a target that went away between the binding's check and this lookup) is a refusal: `false`,
        // which the binding raises as "<Method>: the creature has no AI".
        if (name == "creature.attackStart")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* victim = unitOf(other))
                    if (CreatureAI* ai = creature->AI())
                        // the core's Attack() links two units without a map check: a victim on another map would
                        // be linked across two map threads
                        if (victim->IsInWorld() && victim->GetMap() == creature->GetMap())
                        {
                            ai->AttackStart(victim);
                            return true;
                        }
            out.emplace_back(false);
            return true;
        }
        if (name == "creature.selectVictim")
        {
            // the core sends a creature without a target into evade mode through its AI, unchecked: no AI, no call
            if (!creature->AI())
            {
                out.emplace_back(false);
                return true;
            }
            if (Unit* victim = creature->SelectVictim())
                if (victim->IsInWorld())
                    pushHandle(out, handleFor(victim));
            return true;
        }
        if (name == "creature.callAssistance")
        {
            creature->CallAssistance();
            return true;
        }
        if (name == "creature.callForHelp")
        {
            creature->CallForHelp(static_cast<float>(dArg(args, 0)));
            return true;
        }
        if (name == "creature.fleeToGetAssistance")
        {
            // a creature-AI call: a pet or a player-controlled creature would run away from its owner
            if (creature->IsPet() || creature->IsControlledByPlayer())
            {
                out.emplace_back(false);
                return true;
            }
            creature->DoFleeToGetAssistance();
            return true;
        }
        if (name == "creature.setNoCallAssistance")
        {
            creature->SetNoCallAssistance(bArg(args, 0, true));
            return true;
        }
        if (name == "creature.setNoSearchAssistance")
        {
            creature->SetNoSearchAssistance(bArg(args, 0, true));
            return true;
        }
        if (name == "creature.hasSearchedAssistance")
        {
            out.emplace_back(creature->HasSearchedAssistance());
            return true;
        }
        if (name == "creature.isInEvadeMode")
        {
            out.emplace_back(creature->IsInEvadeMode());
            return true;
        }
        if (name == "creature.setInCombatWithZone")
        {
            // the core's own zone-wide engage (a dungeon map only; it logs an error and does nothing elsewhere); a pet or
            // a player-controlled creature would engage every player of the instance
            if (creature->IsPet() || creature->IsControlledByPlayer())
            {
                out.emplace_back(false);
                return true;
            }
            CreatureAI::DoZoneInCombat(creature);
            return true;
        }
        if (name == "creature.getAggroRange" || name == "creature.getAttackDistance")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* target = unitOf(other))
                {
                    float const range = name == "creature.getAggroRange" ? creature->GetAggroRange(target)
                                                                        : creature->GetAttackDistance(target);
                    out.emplace_back(static_cast<double>(range));
                }
            return true;
        }
        if (name == "creature.canAssistTo")
        {
            HandleData friendArg, enemyArg;
            if (args.size() > 1 && asHandle(args[0], friendArg) && asHandle(args[1], enemyArg))
                if (Unit* friendUnit = unitOf(friendArg))
                    if (Unit* enemyUnit = unitOf(enemyArg))
                    {
                        out.emplace_back(creature->CanAssistTo(friendUnit, enemyUnit, bArg(args, 2, true)));
                    }
            return true;
        }
        if (name == "creature.canStartAttack")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* target = unitOf(other))
                    out.emplace_back(creature->CanStartAttack(target, bArg(args, 1, true)));
            return true;
        }
        if (name == "creature.isTappedBy")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Player* player = playerOf(other))
                    out.emplace_back(creature->isTappedBy(player));
            return true;
        }
        if (name == "creature.hasLootRecipient")
        {
            out.emplace_back(creature->hasLootRecipient());
            return true;
        }
        if (name == "creature.getLootRecipient" || name == "creature.getLootRecipientGroup")
        {
            // the tap list (an unordered set) holds the players that may loot; the recipient is the one with the lowest
            // guid that is in the world, so the same list always answers the same player
            lootRecipientFromTapList(creature, creature->GetTapList(), name == "creature.getLootRecipientGroup", out);
            return true;
        }

        // ---- threat ----
        if (name == "creature.addThreat")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* victim = unitOf(other))
                {
                    SpellInfo const* spell = nullptr;
                    if (uint32 const id = static_cast<uint32>(iArg(args, 3)); id)
                    {
                        spell = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
                        if (!spell)
                        {
                            out.emplace_back(false);
                            return true;
                        }
                    }
                    // this core applies its threat modifiers from the spell, not a school mask: a mask without a
                    // spell entry cannot be honored (refused rather than silently applied as normal-school threat)
                    if (!spell && static_cast<uint32>(iArg(args, 2)) != 0)
                    {
                        out.emplace_back(false);
                        return true;
                    }
                    creature->GetThreatManager().AddThreat(victim, static_cast<float>(dArg(args, 1)), spell);
                    return true;
                }
            out.emplace_back(false);
            return true;
        }
        if (name == "creature.getThreat")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* victim = unitOf(other))
                    out.emplace_back(static_cast<double>(creature->GetThreatManager().GetThreat(victim)));
            return true;
        }
        if (name == "creature.clearThreat")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* victim = unitOf(other))
                    creature->GetThreatManager().ClearThreat(victim);
            return true;
        }
        if (name == "creature.clearThreatList")
        {
            creature->GetThreatManager().ClearAllThreat();
            return true;
        }
        if (name == "creature.resetAllThreat")
        {
            creature->GetThreatManager().ResetAllThreat();
            return true;
        }
        if (name == "creature.fixateTarget")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* target = unitOf(other))
                    creature->GetThreatManager().FixateTarget(target);
            return true;
        }
        if (name == "creature.clearFixate")
        {
            creature->GetThreatManager().ClearFixate();
            return true;
        }
        if (name == "creature.getAITargets")
        {
            for (auto const& ref : creature->GetThreatManager().GetUnsortedThreatList())
            {
                Unit* victim = ref->GetVictim();
                if (victim && victim->IsInWorld())
                    pushHandle(out, handleFor(victim));
            }
            return true;
        }
        if (name == "creature.getAITargetsCount")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetThreatManager().GetThreatListSize()));
            return true;
        }

        // ---- state ----
        CreatureTemplate const* cinfo = creature->GetCreatureTemplate();
        if (name == "creature.isCivilian")
        {
            out.emplace_back(creature->IsCivilian());
            return true;
        }
        if (name == "creature.isGuard")
        {
            out.emplace_back(creature->IsGuard());
            return true;
        }
        if (name == "creature.isRacialLeader")
        {
            out.emplace_back(creature->IsRacialLeader());
            return true;
        }
        if (name == "creature.isTrigger")
        {
            out.emplace_back(creature->IsTrigger());
            return true;
        }
        if (name == "creature.isDungeonBoss")
        {
            out.emplace_back(creature->IsDungeonBoss());
            return true;
        }
        if (name == "creature.isRegeneratingHealth")
        {
            out.emplace_back(creature->CanRegenerateHealth());
            return true;
        }
        if (name == "creature.setRegeneratingHealth")
        {
            // the setter only moves the no-regen flag; the getter also reads the template's own setting, so a value
            // the getter would not read back is refused
            bool const enable = bArg(args, 0, true);
            creature->SetRegenerateHealth(enable);
            if (creature->CanRegenerateHealth() != enable)
            {
                out.emplace_back(false);
                return true;
            }
            return true;
        }
        if (name == "creature.canFly")
        {
            out.emplace_back(creature->CanFly());
            return true;
        }
        if (name == "creature.canSwim")
        {
            out.emplace_back(creature->CanSwim());
            return true;
        }
        if (name == "creature.setHover")
        {
            creature->SetHover(bArg(args, 0, true));
            return true;
        }
        if (name == "creature.setDisableGravity")
        {
            creature->SetDisableGravity(bArg(args, 0, false));
            return true;
        }
        if (name == "creature.isReputationGainDisabled")
        {
            out.emplace_back(creature->IsReputationGainDisabled());
            return true;
        }
        if (name == "creature.setDisableReputationGain")
        {
            creature->SetDisableReputationGain(bArg(args, 0, true));
            return true;
        }
        if (name == "creature.getShieldBlockValue")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetClassicShieldBlockValue()));
            return true;
        }
        if (name == "creature.setWalk")
        {
            creature->SetWalk(bArg(args, 0, true));
            return true;
        }

        // ---- spawn and movement ----
        if (name == "creature.getWaypointPath")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetWaypointPathId()));
            return true;
        }
        if (name == "creature.getCurrentWaypointId")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetCurrentWaypointInfo().first));
            return true;
        }
        if (name == "creature.moveWaypoint")
        {
            uint32 const pathId = creature->GetWaypointPathId();
            // the core starts a spawn's waypoint path through sWaypointMgr: a path that is not loaded is refused
            if (!pathId || !sWaypointMgr->GetPath(pathId))
            {
                out.emplace_back(false);
                return true;
            }
            creature->GetMotionMaster()->MovePath(pathId, true);   // repeatable, as the core's own waypoint factory
            return true;
        }
        if (name == "creature.getDefaultMovementType")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetDefaultMovementType()));
            return true;
        }
        if (name == "creature.setDefaultMovementType")
        {
            // a spawn's default movement: a pet or a player-controlled creature follows its owner instead
            if (creature->IsPet() || creature->IsControlledByPlayer())
            {
                out.emplace_back(false);
                return true;
            }
            creature->SetDefaultMovementType(static_cast<MovementGeneratorType>(iArg(args, 0)));
            return true;
        }
        if (name == "creature.getWanderRadius")
        {
            out.emplace_back(static_cast<double>(creature->GetWanderDistance()));
            return true;
        }
        if (name == "creature.setWanderRadius")
        {
            creature->SetWanderDistance(static_cast<float>(dArg(args, 0)));
            return true;
        }
        if (name == "creature.setHomePosition")
        {
            float const x = static_cast<float>(dArg(args, 0)), y = static_cast<float>(dArg(args, 1));
            float const z = static_cast<float>(dArg(args, 2)), o = static_cast<float>(dArg(args, 3));
            if (!Trinity::IsValidMapCoord(x, y, z, o))   // the core's own coordinate check
            {
                out.emplace_back(false);
                return true;
            }
            creature->SetHomePosition(x, y, z, o);
            return true;
        }
        if (name == "creature.getRespawnDelay")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetRespawnDelay()));
            return true;
        }
        if (name == "creature.getCorpseDelay")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetCorpseDelay()));
            return true;
        }
        if (name == "creature.removeCorpse")
        {
            creature->RemoveCorpse();
            return true;
        }
        if (name == "creature.setDeathState")
        {
            DeathState const state = static_cast<DeathState>(iArg(args, 0));
            // ALIVE only flips the state: a dead creature (0 health) would be "alive" with nothing to stand on.
            // JUST_RESPAWNED is the core's way back (it restores the spawn health).
            if (state == ALIVE && creature->GetHealth() == 0)
            {
                out.emplace_back(false);
                return true;
            }
            // CORPSE and DEAD only store the state: on a living creature that leaves full health and no corpse timer,
            // so it is removed at once. JUST_DIED is the core's way to kill (health, auras, timers, loot).
            if ((state == CORPSE || state == DEAD) && creature->IsAlive())
            {
                out.emplace_back(false);
                return true;
            }
            creature->setDeathState(state);
            return true;
        }
        if (name == "creature.setEquipmentSlots")
        {
            // 0 unequips a slot (the documented meaning); every other entry must be an item the server knows
            for (size_t i = 0; i < 3; ++i)
                if (uint32 const entry = static_cast<uint32>(iArg(args, i)); entry && !sObjectMgr->GetItemTemplate(entry))
                {
                    out.emplace_back(false);
                    return true;
                }
            for (size_t i = 0; i < 3; ++i)
                creature->SetVirtualItem(static_cast<uint32>(i), static_cast<uint32>(iArg(args, i)));
            return true;
        }

        // ---- identity ----
        if (name == "creature.getCreatureFamily" || name == "creature.getExtraFlags" || name == "creature.getRank")
        {
            if (!cinfo)
            {
                out.emplace_back(false);
                return true;
            }
            if (name == "creature.getCreatureFamily")
                out.emplace_back(static_cast<int64_t>(cinfo->family));
            else if (name == "creature.getExtraFlags")
                out.emplace_back(static_cast<int64_t>(cinfo->flags_extra));
            else
                out.emplace_back(static_cast<int64_t>(static_cast<uint32>(cinfo->Classification)));
            return true;
        }
        if (name == "creature.getNPCFlags")
        {
            out.emplace_back(static_cast<int64_t>(static_cast<uint64>(creature->GetNpcFlags())));
            return true;
        }
        if (name == "creature.setNPCFlags")
        {
            // as the core's own UpdateEntry applies a template's flags: the vendor and petitioner flags also carry
            // the update fragment the client reads, so they go through the creature's own setters
            uint32 const flags = static_cast<uint32>(iArg(args, 0));
            // any vendor bit change drops the old vendor fragment first (IsVendor() only reads the generic bit, so a
            // creature that was only a food vendor would keep its stale vendor type data)
            creature->SetVendor(UNIT_NPC_FLAG_VENDOR_MASK, false);
            creature->ReplaceAllNpcFlags(NPCFlags(flags));
            if (flags & UNIT_NPC_FLAG_VENDOR_MASK)
                creature->SetVendor(NPCFlags(flags & UNIT_NPC_FLAG_VENDOR_MASK), true);
            creature->SetPetitioner((flags & UNIT_NPC_FLAG_PETITIONER) != 0);
            return true;
        }
        if (name == "creature.getDBTableGUIDLow")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetSpawnId()));
            return true;
        }
        if (name == "creature.getAIName")
        {
            out.emplace_back(creature->GetAIName());
            return true;
        }
        if (name == "creature.getScriptId")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetScriptId()));
            return true;
        }
        if (name == "creature.updateEntry")
        {
            uint32 const entry = static_cast<uint32>(iArg(args, 0));
            uint64 const dataLow = static_cast<uint64>(iArg(args, 1));
            CreatureTemplate const* newTemplate = sObjectMgr->GetCreatureTemplate(entry);
            if (!newTemplate || creature->IsPet())
            {
                out.emplace_back(false);
                return true;
            }
            // the core's InitEntry changes the entry and template BEFORE it checks for a model and gives up with the
            // creature half switched: refuse an entry it would fail on
            if (!newTemplate->GetFirstValidModel())
            {
                out.emplace_back(false);
                return true;
            }
            CreatureData const* data = nullptr;
            if (dataLow)
            {
                data = sObjectMgr->GetCreatureData(dataLow);
                if (!data)
                {
                    out.emplace_back(false);
                    return true;
                }
            }
            if (CreatureModel const* chosen = ObjectMgr::ChooseDisplayId(newTemplate, data))
            {
                CreatureModel model = *chosen;
                if (!sObjectMgr->GetCreatureModelRandomGender(&model, newTemplate))   // InitEntry's second give-up
                {
                    out.emplace_back(false);
                    return true;
                }
            }
            else
            {
                out.emplace_back(false);
                return true;
            }
            if (!creature->UpdateEntry(entry, data))
            {
                out.emplace_back(false);
                return true;
            }
            return true;
        }

        // ---- spells and cooldowns ----
        if (name == "creature.hasSpell")
        {
            out.emplace_back(creature->HasSpell(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "creature.hasSpellCooldown")
        {
            // the core's HasCooldown(spellId) asserts on an unknown spell: look it up first (1 / 0 / -1)
            SpellInfo const* info = sSpellMgr->GetSpellInfo(static_cast<uint32>(iArg(args, 0)), DIFFICULTY_NONE);
            if (!info)
            {
                out.emplace_back(static_cast<int64_t>(-1));
                return true;
            }
            out.emplace_back(static_cast<int64_t>(creature->GetSpellHistory()->HasCooldown(info) ? 1 : 0));
            return true;
        }
        if (name == "creature.hasCategoryCooldown")
        {
            // 1 / 0 / -1 (unknown spell): the answer is a bool for the script, so the third state is an integer here
            SpellInfo const* info = sSpellMgr->GetSpellInfo(static_cast<uint32>(iArg(args, 0)), DIFFICULTY_NONE);
            if (!info)
            {
                out.emplace_back(static_cast<int64_t>(-1));
                return true;
            }
            out.emplace_back(static_cast<int64_t>(
                creature->GetSpellHistory()->GetRemainingCategoryCooldown(info).count() > 0 ? 1 : 0));
            return true;
        }
        if (name == "creature.getSpellCooldownDelay")
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(static_cast<uint32>(iArg(args, 0)), DIFFICULTY_NONE);
            if (!info)
            {
                out.emplace_back(false);
                return true;
            }
            out.emplace_back(static_cast<int64_t>(creature->GetSpellHistory()->GetRemainingCooldown(info).count()));
            return true;
        }

        // ---- loot mode ----
        if (name == "creature.getLootMode")
        {
            out.emplace_back(static_cast<int64_t>(creature->GetLootMode()));
            return true;
        }
        if (name == "creature.hasLootMode")
        {
            out.emplace_back(creature->HasLootMode(static_cast<uint16>(iArg(args, 0))));
            return true;
        }
        if (name == "creature.setLootMode")
        {
            creature->SetLootMode(static_cast<uint16>(iArg(args, 0)));
            return true;
        }
        if (name == "creature.addLootMode")
        {
            creature->AddLootMode(static_cast<uint16>(iArg(args, 0)));
            return true;
        }
        if (name == "creature.removeLootMode")
        {
            creature->RemoveLootMode(static_cast<uint16>(iArg(args, 0)));
            return true;
        }
        if (name == "creature.resetLootMode")
        {
            creature->ResetLootMode();
            return true;
        }

        // ---- quests ----
        if (name == "creature.hasQuest")
        {
            out.emplace_back(creature->hasQuest(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
    }

    // ---- GameObject ----
    GameObject* go = goOf(self);
    if (go)
    {
        if (name == "gameobject.getGoState")
        {
            out.emplace_back(static_cast<int64_t>(go->GetGoState()));
            return true;
        }
        if (name == "gameobject.setGoState")
        {
            go->SetGoState(static_cast<GOState>(iArg(args, 0)));
            return true;
        }
        if (name == "gameobject.getLootState")
        {
            out.emplace_back(static_cast<int64_t>(go->getLootState()));
            return true;
        }
        if (name == "gameobject.setLootState")
        {
            go->SetLootState(static_cast<LootState>(iArg(args, 0)));
            return true;
        }
        if (name == "gameobject.despawn")
        {
            go->Delete();
            return true;
        }
        if (name == "gameobject.respawn")
        {
            go->Respawn();
            return true;
        }
        if (name == "gameobject.isSpawned")
        {
            out.emplace_back(go->isSpawned());
            return true;
        }
        if (name == "gameobject.useDoorOrButton")
        {
            go->UseDoorOrButton(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "gameobject.getDBTableGUIDLow")
        {
            out.emplace_back(static_cast<int64_t>(go->GetSpawnId()));
            return true;
        }
        if (name == "gameobject.getDisplayId")
        {
            out.emplace_back(static_cast<int64_t>(go->GetDisplayId()));
            return true;
        }
        if (name == "gameobject.getLootRecipient" || name == "gameobject.getLootRecipientGroup")
        {
            lootRecipientFromTapList(go, go->GetTapList(), name == "gameobject.getLootRecipientGroup", out);
            return true;
        }
        if (name == "gameobject.hasQuest")
        {
            out.emplace_back(go->hasQuest(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "gameobject.isActive")
        {
            out.emplace_back(go->isActiveObject());
            return true;
        }
        if (name == "gameobject.isDestructible")
        {
            out.emplace_back(go->IsDestructibleBuilding());
            return true;
        }
        if (name == "gameobject.isTransport")
        {
            out.emplace_back(go->IsTransport());
            return true;
        }
        if (name == "gameobject.removeFromWorld")
        {
            go->AddObjectToRemoveList();
            return true;
        }
        if (name == "gameobject.setRespawnTime")
        {
            go->SetRespawnTime(static_cast<int32>(iArg(args, 0, 0)));
            return true;
        }
        if (name == "gameobject.setSpawnedByDefault")
        {
            go->SetSpawnedByDefault(bArg(args, 0, true));
            return true;
        }
    }

    // ---- Player ----
    Player* player = playerOf(self);
    if (player)
    {
        if (name == "player.broadcast" || name == "player.notification")
        {
            std::string const msg = sArg(args, 0);
            if (player->GetSession())
                ChatHandler(player->GetSession()).SendSysMessage(msg);
            return true;
        }
        if (name == "player.getAccountId")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSession() ? player->GetSession()->GetAccountId() : 0));
            return true;
        }
        if (name == "player.getAccountName")
        {
            out.emplace_back(player->GetSession() ? player->GetSession()->GetAccountName() : std::string());
            return true;
        }
        if (name == "player.getGMRank")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSession() ? player->GetSession()->GetSecurity() : 0));
            return true;
        }
        if (name == "player.isGM")
        {
            out.emplace_back(player->IsGameMaster());
            return true;
        }
        if (name == "player.getTeam")
        {
            out.emplace_back(static_cast<int64_t>(player->GetTeamId()));
            return true;
        }
        if (name == "player.getCoinage")
        {
            out.emplace_back(static_cast<int64_t>(player->GetMoney()));
            return true;
        }
        if (name == "player.modifyMoney")
        {
            player->ModifyMoney(iArg(args, 0));
            return true;
        }
        if (name == "player.addItem")
        {
            uint32 const entry = static_cast<uint32>(iArg(args, 0));
            uint32 const count = static_cast<uint32>(std::max<int64_t>(1, iArg(args, 1, 1)));
            ItemPosCountVec dest;
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, count) == EQUIP_ERR_OK)
                if (Item* item = player->StoreNewItem(dest, entry, true))
                {
                    rememberItem(item);
                    pushHandle(out, handleForItem(item));
                }
            return true;
        }
        if (name == "player.removeItem")
        {
            uint32 count = static_cast<uint32>(std::max<int64_t>(1, iArg(args, 1, 1)));
            if (HandleData item; args.size() > 0 && asHandle(args[0], item))
            {
                if (Item* i = findItem(item))
                    player->DestroyItemCount(i, count, true);
            }
            else
                player->DestroyItemCount(static_cast<uint32>(iArg(args, 0)), count, true);
            return true;
        }
        if (name == "player.hasItem")
        {
            out.emplace_back(player->HasItemCount(static_cast<uint32>(iArg(args, 0)),
                                                  static_cast<uint32>(std::max<int64_t>(1, iArg(args, 1, 1))),
                                                  bArg(args, 2, false)));
            return true;
        }
        if (name == "player.getItemCount")
        {
            out.emplace_back(static_cast<int64_t>(player->GetItemCount(static_cast<uint32>(iArg(args, 0)),
                                                                        bArg(args, 1, false))));
            return true;
        }
        if (name == "player.teleport")
        {
            out.emplace_back(player->TeleportTo(static_cast<uint32>(iArg(args, 0)), static_cast<float>(dArg(args, 1)),
                                                static_cast<float>(dArg(args, 2)), static_cast<float>(dArg(args, 3)),
                                                static_cast<float>(dArg(args, 4))));
            return true;
        }
        if (name == "player.giveXP")
        {
            Unit* victim = nullptr;
            if (args.size() > 1)
                if (HandleData h; asHandle(args[1], h))
                    victim = unitOf(h);
            player->GiveXP(static_cast<uint32>(iArg(args, 0)), victim);
            return true;
        }
        if (name == "player.learnSpell")
        {
            player->LearnSpell(static_cast<uint32>(iArg(args, 0)), false);
            return true;
        }
        if (name == "player.removeSpell")
        {
            player->RemoveSpell(static_cast<uint32>(iArg(args, 0)), false, false);
            return true;
        }
        if (name == "player.hasSpell")
        {
            out.emplace_back(player->HasSpell(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.getCorpse")
        {
            if (Corpse* corpse = player->GetCorpse())
                if (corpse->IsInWorld())
                    pushHandle(out, handleForCorpse(corpse));
            return true;
        }
        if (name == "player.getGroup")
        {
            if (Group* g = player->GetGroup())
            {
                HandleData h;
                h.kind = ObjKind::Group;
                h.guid = fromGuid(g->GetGUID());
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "player.getGuild")
        {
            if (Guild* g = player->GetGuild())
            {
                HandleData h;
                h.kind = ObjKind::Guild;
                h.guid.lo = g->GetId();
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "player.getGuildId")
        {
            out.emplace_back(static_cast<int64_t>(player->GetGuildId()));
            return true;
        }
        if (name == "player.getQuestStatus")
        {
            out.emplace_back(static_cast<int64_t>(player->GetQuestStatus(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.addQuest")
        {
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(static_cast<uint32>(iArg(args, 0))))
                player->AddQuest(quest, nullptr);
            return true;
        }
        if (name == "player.completeQuest")
        {
            player->CompleteQuest(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.hasQuest")
        {
            out.emplace_back(player->GetQuestStatus(static_cast<uint32>(iArg(args, 0))) != QUEST_STATUS_NONE);
            return true;
        }
        if (name == "player.saveToDB")
        {
            player->SaveToDB(false);
            return true;
        }
        if (name == "player.kick")
        {
            if (player->GetSession())
                player->GetSession()->KickPlayer("Eluna");
            return true;
        }
        if (name == "player.resurrect")
        {
            player->ResurrectPlayer(static_cast<float>(dArg(args, 0, 100.0)) / 100.0f, bArg(args, 1, false));
            return true;
        }
        if (name == "player.getSelection")
        {
            ObjectGuid const sel = player->GetTarget();
            if (!sel.IsEmpty())
                if (Unit* u = ObjectAccessor::GetUnit(*player, sel))
                    if (u->IsInWorld())
                        pushHandle(out, handleFor(u));
            return true;
        }
        if (name == "player.isAlliance")
        {
            out.emplace_back(player->GetTeamId() == TEAM_ALLIANCE);
            return true;
        }
        if (name == "player.isHorde")
        {
            out.emplace_back(player->GetTeamId() == TEAM_HORDE);
            return true;
        }

        // ---- E5 long tail, round 1: money, level, XP, rest, honor and reputation ----
        if (name == "player.setCoinage")
        {
            player->SetMoney(static_cast<uint64>(std::max<int64_t>(0, iArg(args, 0))));
            return true;
        }
        if (name == "player.getXP")
        {
            out.emplace_back(static_cast<int64_t>(player->GetXP()));
            return true;
        }
        if (name == "player.getXPForNextLevel")
        {
            out.emplace_back(static_cast<int64_t>(player->GetXPForNextLevel()));
            return true;
        }
        if (name == "player.getRestBonus")
        {
            out.emplace_back(static_cast<double>(player->GetRestMgr().GetRestBonus(REST_TYPE_XP)));
            return true;
        }
        if (name == "player.setRestBonus")
        {
            player->GetRestMgr().SetRestBonus(REST_TYPE_XP, static_cast<float>(dArg(args, 0)));
            return true;
        }
        if (name == "player.isRested")
        {
            out.emplace_back(player->HasPlayerFlag(PLAYER_FLAGS_RESTING));
            return true;
        }
        if (name == "player.getHonorPoints")
        {
            out.emplace_back(static_cast<int64_t>(player->GetCurrencyQuantity(CURRENCY_HONOR_CLASSIC)));
            return true;
        }
        if (name == "player.setHonorPoints")
        {
            int64_t const want = std::clamp<int64_t>(iArg(args, 0), 0, std::numeric_limits<uint32>::max());
            int64_t const now = player->GetCurrencyQuantity(CURRENCY_HONOR_CLASSIC);
            if (want != now)
                player->ModifyCurrency(CURRENCY_HONOR_CLASSIC, static_cast<int32>(want - now));
            return true;
        }
        if (name == "player.modifyHonorPoints")
        {
            player->ModifyCurrency(CURRENCY_HONOR_CLASSIC, static_cast<int32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.getReputation")
        {
            out.emplace_back(static_cast<int64_t>(player->GetReputation(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.setReputation")
        {
            // The core dereferences the faction entry: an unknown faction id must never reach it.
            if (sFactionStore.LookupEntry(static_cast<uint32>(iArg(args, 0))))
                player->SetReputation(static_cast<uint32>(iArg(args, 0)), static_cast<int32>(iArg(args, 1)));
            return true;
        }
        if (name == "player.getReputationRank")
        {
            // Unknown faction id: the core's rank lookup dereferences a null entry; answer the lowest rank (hated).
            if (sFactionStore.LookupEntry(static_cast<uint32>(iArg(args, 0))))
                out.emplace_back(static_cast<int64_t>(player->GetReputationRank(static_cast<uint32>(iArg(args, 0)))));
            else
                out.emplace_back(static_cast<int64_t>(MIN_REPUTATION_RANK));
            return true;
        }

        // ---- skills ----
        if (name == "player.getSkillValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSkillValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.getMaxSkillValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetMaxSkillValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.getPureSkillValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetPureSkillValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.getPureMaxSkillValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetPureMaxSkillValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.getBaseSkillValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetBaseSkillValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.getSkillPermBonusValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSkillPermBonusValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.getSkillTempBonusValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSkillTempBonusValue(static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.hasSkill")
        {
            out.emplace_back(player->HasSkill(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.setSkill")
        {
            player->SetSkill(static_cast<uint32>(iArg(args, 0)), static_cast<uint16>(iArg(args, 1)),
                             static_cast<uint16>(iArg(args, 2)), static_cast<uint16>(iArg(args, 3)));
            return true;
        }

        // ---- spells and cooldowns ----
        if (name == "player.resetSpellCooldown")
        {
            player->GetSpellHistory()->ResetCooldown(static_cast<uint32>(iArg(args, 0)), bArg(args, 1, true));
            return true;
        }
        if (name == "player.hasSpellCooldown")
        {
            out.emplace_back(player->GetSpellHistory()->HasCooldown(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.getSpellCooldownDelay")
        {
            Milliseconds remaining{0};
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(static_cast<uint32>(iArg(args, 0)), DIFFICULTY_NONE))
                remaining = player->GetSpellHistory()->GetRemainingCooldown(info);
            out.emplace_back(static_cast<int64_t>(remaining.count()));
            return true;
        }
        if (name == "player.resetAllCooldowns")
        {
            player->GetSpellHistory()->ResetAllCooldowns();
            return true;
        }
        if (name == "player.resetTypeCooldowns")
        {
            uint32 const category = static_cast<uint32>(iArg(args, 0));
            player->GetSpellHistory()->ResetCooldowns(
                [category](SpellHistory::CooldownEntry const& e) { return e.CategoryId == category; },
                bArg(args, 1, true));
            return true;
        }

        // ---- items ----
        if (name == "player.getItemByPos")
        {
            if (Item* item = player->GetItemByPos(static_cast<uint8>(iArg(args, 0)), static_cast<uint8>(iArg(args, 1))))
            {
                rememberItem(item);
                pushHandle(out, handleForItem(item));
            }
            return true;
        }
        if (name == "player.getItemByEntry")
        {
            if (Item* item = player->GetItemByEntry(static_cast<uint32>(iArg(args, 0)), ItemSearchLocation::Everywhere))
            {
                rememberItem(item);
                pushHandle(out, handleForItem(item));
            }
            return true;
        }
        if (name == "player.getItemByGUID")
        {
            fl::Guid g;
            if (fl::parseGuidText(sArg(args, 0), g))
            {
                // A 64-bit number (the docs type) carries only the low counter: the high half is the item type here.
                ObjectGuid guid = g.hi == 0 ? ObjectGuid::Create<HighGuid::Item>(g.lo) : toGuid(g);
                if (Item* item = player->GetItemByGuid(guid))
                {
                    rememberItem(item);
                    pushHandle(out, handleForItem(item));
                }
            }
            return true;
        }
        if (name == "player.getEquippedItemBySlot")
        {
            uint8 const slot = static_cast<uint8>(iArg(args, 0));
            if (slot < EQUIPMENT_SLOT_END)
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    rememberItem(item);
                    pushHandle(out, handleForItem(item));
                }
            return true;
        }
        if (name == "player.equipItem")
        {
            uint8 const slot = static_cast<uint8>(iArg(args, 1));
            uint16 dest = 0;
            Item* equipped = nullptr;
            if (HandleData itemHandle; args.size() > 0 && asHandle(args[0], itemHandle))
            {
                Item* item = findItem(itemHandle);
                if (item && player->CanEquipItem(slot, dest, item, false) == EQUIP_ERR_OK)
                {
                    // Player::EquipItem expects an item that sits in no slot (the core's own auto-equip removes it
                    // first): take it out of its bag/bank position, or it would exist in two places.
                    player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
                    equipped = player->EquipItem(dest, item, true);
                }
            }
            else if (player->CanEquipNewItem(slot, dest, static_cast<uint32>(iArg(args, 0)), false) == EQUIP_ERR_OK)
                equipped = player->EquipNewItem(dest, static_cast<uint32>(iArg(args, 0)), ItemContext::NONE, true);
            if (equipped)
            {
                rememberItem(equipped);
                pushHandle(out, handleForItem(equipped));
            }
            return true;
        }
        if (name == "player.canEquipItem")
        {
            uint8 const slot = static_cast<uint8>(iArg(args, 1));
            uint16 dest = 0;
            bool can = false;
            if (HandleData itemHandle; args.size() > 0 && asHandle(args[0], itemHandle))
            {
                if (Item* item = findItem(itemHandle))
                    can = player->CanEquipItem(slot, dest, item, false) == EQUIP_ERR_OK;
            }
            else
                can = player->CanEquipNewItem(slot, dest, static_cast<uint32>(iArg(args, 0)), false) == EQUIP_ERR_OK;
            out.emplace_back(can);
            return true;
        }
        if (name == "player.canUseItem")
        {
            bool can = false;
            if (HandleData itemHandle; args.size() > 0 && asHandle(args[0], itemHandle))
            {
                if (Item* item = findItem(itemHandle))
                    can = player->CanUseItem(item) == EQUIP_ERR_OK;
            }
            else if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(static_cast<uint32>(iArg(args, 0))))
                can = player->CanUseItem(proto) == EQUIP_ERR_OK;
            out.emplace_back(can);
            return true;
        }

        // ---- quests ----
        if (name == "player.getQuestRewardStatus")
        {
            out.emplace_back(player->GetQuestRewardStatus(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.incompleteQuest")
        {
            player->IncompleteQuest(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.failQuest")
        {
            player->FailQuest(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.removeQuest")
        {
            // The docs say only "removes the given quest entry from the Player": take it out of the log and out of the
            // rewarded set, so that either half of the quest's state is gone.
            uint32 const questId = static_cast<uint32>(iArg(args, 0));
            player->RemoveActiveQuest(questId);
            player->RemoveRewardedQuest(questId);
            return true;
        }
        if (name == "player.removeActiveQuest")
        {
            player->RemoveActiveQuest(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.removeRewardedQuest")
        {
            player->RemoveRewardedQuest(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.canCompleteQuest")
        {
            out.emplace_back(player->CanCompleteQuest(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.canRewardQuest")
        {
            bool can = false;
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(static_cast<uint32>(iArg(args, 0))))
                can = player->CanRewardQuest(quest, false);
            out.emplace_back(can);
            return true;
        }
        if (name == "player.rewardQuest")
        {
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(static_cast<uint32>(iArg(args, 0))))
                player->RewardQuest(quest, LootItemType::Item, 0, nullptr, false);
            return true;
        }
        if (name == "player.areaExploredOrEventHappens")
        {
            player->AreaExploredOrEventHappens(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.killedMonsterCredit")
        {
            player->KilledMonsterCredit(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.groupEventHappens")
        {
            WorldObject* eventObject = nullptr;
            if (HandleData objectHandle; args.size() > 1 && asHandle(args[1], objectHandle))
                eventObject = woOf(objectHandle);
            player->GroupEventHappens(static_cast<uint32>(iArg(args, 0)), eventObject);
            return true;
        }
        if (name == "player.talkedToCreature")
        {
            uint32 const entry = static_cast<uint32>(iArg(args, 0));
            ObjectGuid guid;
            if (HandleData creatureHandle; args.size() > 1 && asHandle(args[1], creatureHandle))
                guid = toGuid(creatureHandle.guid);
            player->TalkedToCreature(entry, guid);
            return true;
        }
        if (name == "player.hasQuestForGO")
        {
            out.emplace_back(player->HasQuestForGO(static_cast<int32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.hasQuestForItem")
        {
            out.emplace_back(player->HasQuestForItem(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.getReqKillOrCastCurrentCount")
        {
            out.emplace_back(static_cast<int64_t>(player->GetReqKillOrCastCurrentCount(
                static_cast<uint32>(iArg(args, 0)), static_cast<int32>(iArg(args, 1)))));
            return true;
        }
        if (name == "player.hasReceivedQuestReward")
        {
            out.emplace_back(player->IsQuestRewarded(static_cast<uint32>(iArg(args, 0))));
            return true;
        }

        // ---- state ----
        if (name == "player.isInGroup")
        {
            out.emplace_back(player->GetGroup() != nullptr);
            return true;
        }
        if (name == "player.isInGuild")
        {
            out.emplace_back(player->GetGuildId() != 0);
            return true;
        }
        if (name == "player.isFlying")
        {
            out.emplace_back(player->IsFlying());
            return true;
        }
        if (name == "player.isMoving")
        {
            out.emplace_back(player->isMoving());
            return true;
        }
        if (name == "player.isFalling")
        {
            out.emplace_back(player->IsFalling());
            return true;
        }
        if (name == "player.isInWater")
        {
            out.emplace_back(player->IsInWater());
            return true;
        }
        if (name == "player.isTaxiCheater")
        {
            out.emplace_back(player->isTaxiCheater());
            return true;
        }
        if (name == "player.setTaxiCheat")
        {
            player->SetTaxiCheater(bArg(args, 0, true));
            return true;
        }
        if (name == "player.inBattleground")
        {
            out.emplace_back(player->InBattleground());
            return true;
        }
        if (name == "player.getBattlegroundId")
        {
            out.emplace_back(static_cast<int64_t>(player->GetBattlegroundId()));
            return true;
        }
        if (name == "player.getBattlegroundTypeId")
        {
            out.emplace_back(static_cast<int64_t>(player->GetBattlegroundTypeId()));
            return true;
        }
        if (name == "player.isAFK")
        {
            out.emplace_back(player->isAFK());
            return true;
        }
        if (name == "player.isDND")
        {
            out.emplace_back(player->isDND());
            return true;
        }
        if (name == "player.toggleAFK")
        {
            player->ToggleAFK();
            return true;
        }
        if (name == "player.toggleDND")
        {
            player->ToggleDND();
            return true;
        }
        if (name == "player.isImmuneToDamage")
        {
            out.emplace_back(player->IsImmunedToDamage(SPELL_SCHOOL_MASK_ALL));
            return true;
        }
        if (name == "player.isImmuneToEnvironmentalDamage")
        {
            out.emplace_back(player->IsImmuneToEnvironmentalDamage());
            return true;
        }

        // ---- chat ----
        if (name == "player.say" || name == "player.yell")
        {
            std::string const text = sArg(args, 0);
            int64_t const langArg = iArg(args, 1);
            Language language = LANG_UNIVERSAL;
            if (langArg >= 0 && langArg <= LANG_HARANI)
                language = static_cast<Language>(langArg);
            if (name == "player.say")
                player->Say(text, language);
            else
                player->Yell(text, language);
            return true;
        }
        if (name == "player.whisper")
        {
            std::string const text = sArg(args, 0);
            int64_t const langArg = iArg(args, 1);
            Language language = LANG_UNIVERSAL;
            if (langArg >= 0 && langArg <= LANG_HARANI)
                language = static_cast<Language>(langArg);
            Player* receiver = nullptr;
            if (HandleData r; args.size() > 2 && asHandle(args[2], r))
                receiver = playerOf(r);
            else if (args.size() > 2)
            {
                fl::Guid g;
                if (fl::parseGuidText(sArg(args, 2), g))
                    receiver = ObjectAccessor::FindConnectedPlayer(g.hi == 0 ? ObjectGuid::Create<HighGuid::Player>(g.lo)
                                                                            : toGuid(g));
            }
            if (receiver)
                player->Whisper(text, language, receiver);
            return true;
        }
        if (name == "player.textEmote")
        {
            player->TextEmote(sArg(args, 0));
            return true;
        }

        // ---- UI sends ----
        if (name == "player.sendShowBank")
        {
            WorldObject* sender = nullptr;
            if (HandleData h; args.size() > 0 && asHandle(args[0], h))
                sender = woOf(h);
            if (sender && player->GetSession())
                player->GetSession()->SendShowBank(sender->GetGUID(), PlayerInteractionType::Banker);
            return true;
        }
        if (name == "player.sendListInventory")
        {
            WorldObject* sender = nullptr;
            if (HandleData h; args.size() > 0 && asHandle(args[0], h))
                sender = woOf(h);
            if (sender && player->GetSession())
                player->GetSession()->SendListInventory(sender->GetGUID());
            return true;
        }
        if (name == "player.sendTrainerList")
        {
            Creature* trainer = nullptr;
            if (HandleData h; args.size() > 0 && asHandle(args[0], h))
                trainer = creatureOf(h);
            if (trainer && player->GetSession())
                if (uint32 trainerId = trainer->GetTrainerId())
                    player->GetSession()->SendTrainerList(trainer, trainerId);
            return true;
        }
        if (name == "player.sendTaxiMenu")
        {
            Creature* taxi = nullptr;
            if (HandleData h; args.size() > 0 && asHandle(args[0], h))
                taxi = creatureOf(h);
            if (taxi && player->GetSession())
                player->GetSession()->SendTaxiMenu(taxi);
            return true;
        }

        // ---- bind and summon ----
        if (name == "player.setBindPoint")
        {
            if (!sMapStore.LookupEntry(static_cast<uint32>(iArg(args, 3))))
                return true; // a bind point on a map that does not exist would strand the character at login
            WorldLocation const location(static_cast<uint32>(iArg(args, 3)), static_cast<float>(dArg(args, 0)),
                                         static_cast<float>(dArg(args, 1)), static_cast<float>(dArg(args, 2)), 0.0f);
            player->SetHomebind(location, static_cast<uint32>(iArg(args, 4)));
            player->SendBindPointUpdate();
            return true;
        }
        if (name == "player.summonPlayer")
        {
            Unit* summoner = nullptr;
            if (HandleData h; args.size() > 0 && asHandle(args[0], h))
                summoner = unitOf(h);
            if (summoner)
                player->SendSummonRequestFrom(summoner);
            return true;
        }

        // ---- misc ----
        if (name == "player.getGuildName")
        {
            // Player::GetGuildName dereferences the guild lookup; go through GetGuild() so a guild that is not loaded gives "".
            out.emplace_back(player->GetGuild() ? player->GetGuild()->GetName() : std::string());
            return true;
        }
        if (name == "player.getDbLocaleIndex")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSession() ? player->GetSession()->GetSessionDbLocaleIndex()
                                                                       : LOCALE_enUS));
            return true;
        }
        if (name == "player.getPlayerIP")
        {
            out.emplace_back(player->GetSession() ? player->GetSession()->GetRemoteAddress() : std::string());
            return true;
        }
        if (name == "player.getLatency")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSession() ? player->GetSession()->GetLatency() : 0));
            return true;
        }
        if (name == "player.getTotalPlayedTime")
        {
            out.emplace_back(static_cast<int64_t>(player->GetTotalPlayedTime()));
            return true;
        }
        if (name == "player.getLevelPlayedTime")
        {
            out.emplace_back(static_cast<int64_t>(player->GetLevelPlayedTime()));
            return true;
        }
        if (name == "player.logoutPlayer")
        {
            // Logging out destroys the player's session and the Player object; a handler must not do that under its own
            // call, so the logout is queued for the world thread and runs there on the next tick (the maps idle).
            bool const save = bArg(args, 0, true);
            fl::Guid const guid = fromGuid(player->GetGUID());
            if (!flh::deferred().push(
                    [guid, save]
                    {
                        if (Player* target = ObjectAccessor::FindPlayer(toGuid(guid)))
                            if (WorldSession* session = target->GetSession())
                                session->LogoutPlayer(save);
                    }))
                log(fl::LogLevel::Warn, "ForeverLua: LogoutPlayer dropped: the world thread queue is full");
            return true;
        }
        if (name == "player.durabilityRepairAll")
        {
            player->DurabilityRepairAll(bArg(args, 0, true), static_cast<float>(dArg(args, 1, 1.0)), bArg(args, 2, false));
            return true;
        }

        // ---- E7 long tail, round 2: the documented reads the core answers directly ----
        if (name == "player.canBlock")
        {
            out.emplace_back(player->CanBlock());
            return true;
        }
        if (name == "player.canCompleteRepeatableQuest")
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(static_cast<uint32>(iArg(args, 0)));
            out.emplace_back(quest && player->CanCompleteRepeatableQuest(quest));
            return true;
        }
        if (name == "player.canFly")
        {
            out.emplace_back(player->CanFly());
            return true;
        }
        if (name == "player.canParry")
        {
            out.emplace_back(player->CanParry());
            return true;
        }
        if (name == "player.canShareQuest")
        {
            out.emplace_back(player->CanShareQuest(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.canSpeak")
        {
            out.emplace_back(player->GetSession() && player->GetSession()->CanSpeak());
            return true;
        }
        if (name == "player.canUninviteFromGroup")
        {
            fl::Guid guid;
            // The docs' PartyResult: an unreadable guid is the "no such member" answer, not an error.
            uint8 result = static_cast<uint8>(ERR_TARGET_NOT_IN_GROUP_S);
            if (args.size() > 0 && fl::parseGuidText(sArg(args, 0), guid))
                result = static_cast<uint8>(player->CanUninviteFromGroup(toGuid(guid), {}));
            out.emplace_back(static_cast<int64_t>(result));
            return true;
        }
        if (name == "player.getChatTag")
        {
            // The documented call is the active GM chat tag; this core keeps the GM chat toggle and no tag byte.
            out.emplace_back(static_cast<int64_t>(player->isGMChat() ? 1 : 0));
            return true;
        }
        if (name == "player.getChampioningFaction")
        {
            out.emplace_back(static_cast<int64_t>(player->GetChampioningFaction()));
            return true;
        }
        if (name == "player.getDbcLocale")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSession() ? player->GetSession()->GetSessionDbcLocale()
                                                                       : LOCALE_enUS));
            return true;
        }
        if (name == "player.getDifficulty")
        {
            out.emplace_back(static_cast<int64_t>(player->GetMap()->GetDifficultyID()));
            return true;
        }
        if (name == "player.getDrunkValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetDrunkValue()));
            return true;
        }
        if (name == "player.getGroupInvite")
        {
            if (Group* g = player->GetGroupInvite())
            {
                HandleData h;
                h.kind = ObjKind::Group;
                h.guid = fromGuid(g->GetGUID());
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "player.getGuildRank")
        {
            out.emplace_back(static_cast<int64_t>(player->GetGuildRank()));
            return true;
        }
        if (name == "player.getHealthBonusFromStamina")
        {
            out.emplace_back(static_cast<double>(player->GetHealthBonusFromStamina()));
            return true;
        }
        if (name == "player.getInGameTime")
        {
            out.emplace_back(static_cast<int64_t>(player->GetInGameTime()));
            return true;
        }
        if (name == "player.getLifetimeKills")
        {
            out.emplace_back(static_cast<int64_t>(player->m_activePlayerData->LifetimeHonorableKills));
            return true;
        }
        if (name == "player.getMailCount")
        {
            out.emplace_back(static_cast<int64_t>(player->GetMailSize()));
            return true;
        }
        if (name == "player.getMailItem")
        {
            // A mailbox attachment is a live item while it sits in the mailbox: Player::GetMItem keys it by its low guid.
            fl::Guid g;
            if (fl::parseGuidText(sArg(args, 0), g))
                if (Item* item = player->GetMItem(g.lo))
                {
                    rememberItem(item);
                    pushHandle(out, handleForItem(item));
                }
            return true;
        }
        if (name == "player.getManaBonusFromIntellect")
        {
            out.emplace_back(static_cast<double>(player->GetManaBonusFromIntellect()));
            return true;
        }
        if (name == "player.getNearbyGameObject")
        {
            if (player->GetSession())
                if (GameObject* go = ChatHandler(player->GetSession()).GetNearbyGameObject())
                    if (go->IsInWorld())
                        pushHandle(out, handleFor(go));
            return true;
        }
        if (name == "player.getNextRandomRaidMember")
        {
            if (Player* member = player->GetNextRandomRaidMember(static_cast<float>(dArg(args, 0))))
                pushHandle(out, handleFor(member));
            return true;
        }
        if (name == "player.getOriginalGroup")
        {
            if (Group* g = player->GetOriginalGroup())
            {
                HandleData h;
                h.kind = ObjKind::Group;
                h.guid = fromGuid(g->GetGUID());
                pushHandle(out, h);
            }
            return true;
        }
        if (name == "player.getOriginalSubGroup")
        {
            out.emplace_back(static_cast<int64_t>(player->GetOriginalSubGroup()));
            return true;
        }
        if (name == "player.getQuestLevel")
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(static_cast<uint32>(iArg(args, 0)));
            out.emplace_back(static_cast<int64_t>(quest ? player->GetQuestLevel(quest) : 0));
            return true;
        }
        if (name == "player.getRecruiterId")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSession() ? player->GetSession()->GetRecruiterId() : 0));
            return true;
        }
        if (name == "player.getSelectedPlayer")
        {
            if (Player* selected = player->GetSelectedPlayer())
                if (selected->IsInWorld())
                    pushHandle(out, handleFor(selected));
            return true;
        }
        if (name == "player.getSelectedUnit")
        {
            if (Unit* selected = player->GetSelectedUnit())
                if (selected->IsInWorld())
                    pushHandle(out, handleFor(selected));
            return true;
        }
        if (name == "player.getShieldBlockValue")
        {
            out.emplace_back(static_cast<int64_t>(player->GetClassicShieldBlockValue()));
            return true;
        }
        if (name == "player.getSubGroup")
        {
            out.emplace_back(static_cast<int64_t>(player->GetSubGroup()));
            return true;
        }
        if (name == "player.getXPRestBonus")
        {
            out.emplace_back(static_cast<int64_t>(player->GetRestMgr().GetRestBonusFor(
                REST_TYPE_XP, static_cast<uint32>(iArg(args, 0)))));
            return true;
        }
        if (name == "player.hasAtLoginFlag")
        {
            out.emplace_back(player->HasAtLoginFlag(static_cast<AtLoginFlags>(iArg(args, 0))));
            return true;
        }
        if (name == "player.hasPendingBind")
        {
            out.emplace_back(player->HasPendingBind());
            return true;
        }
        if (name == "player.hasTalent")
        {
            out.emplace_back(player->HasTalent(static_cast<uint32>(iArg(args, 0)), static_cast<uint8>(iArg(args, 1))));
            return true;
        }
        if (name == "player.hasTitle")
        {
            out.emplace_back(player->HasTitle(static_cast<uint32>(iArg(args, 0))));
            return true;
        }
        if (name == "player.inArena")
        {
            out.emplace_back(player->InArena());
            return true;
        }
        if (name == "player.inBattlegroundQueue")
        {
            out.emplace_back(player->InBattlegroundQueue(bArg(args, 0, false)));
            return true;
        }
        if (name == "player.isAcceptingWhispers")
        {
            out.emplace_back(player->isAcceptWhispers());
            return true;
        }
        if (name == "player.isARecruiter")
        {
            out.emplace_back(player->GetSession() && player->GetSession()->IsARecruiter());
            return true;
        }
        if (name == "player.isGMChat")
        {
            out.emplace_back(player->isGMChat());
            return true;
        }
        if (name == "player.isGMVisible")
        {
            out.emplace_back(player->isGMVisible());
            return true;
        }
        if (name == "player.isHonorOrXPTarget")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Unit* victim = unitOf(other))
                    out.emplace_back(player->isHonorOrXPTarget(victim));
            return true;
        }
        if (name == "player.isGroupVisibleFor" || name == "player.isInSameGroupWith" ||
            name == "player.isInSameRaidWith")
        {
            HandleData other;
            if (args.size() > 0 && asHandle(args[0], other))
                if (Player* target = playerOf(other))
                {
                    if (name == "player.isGroupVisibleFor")
                        out.emplace_back(player->IsGroupVisibleFor(target));
                    else if (name == "player.isInSameGroupWith")
                        out.emplace_back(player->IsInSameGroupWith(target));
                    else
                        out.emplace_back(player->IsInSameRaidWith(target));
                }
            return true;
        }
        if (name == "player.isNeverVisible")
        {
            // The core answers visibility per seer; the player itself is the seer a no-argument call means.
            out.emplace_back(player->IsNeverVisibleFor(player, false));
            return true;
        }
        if (name == "player.isOutdoorPvPActive")
        {
            out.emplace_back(player->IsOutdoorPvPActive());
            return true;
        }
        if (name == "player.isVisibleForPlayer")
        {
            HandleData other;
            if (!args.empty() && asHandle(args[0], other))
                if (Player* target = playerOf(other))
                    out.emplace_back(player->CanSeeOrDetect(target));
            return true;
        }
        if (name == "player.resetTalentsCost")
        {
            out.emplace_back(static_cast<int64_t>(player->GetNextResetTalentsCost()));
            return true;
        }

        // ---- E7 long tail, round 2: the documented writes that only change the receiver ----
        if (name == "player.sendCinematicStart")
        {
            player->SendCinematicStart(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.sendMovieStart")
        {
            player->SendMovieStart(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.sendUpdateWorldState")
        {
            player->SendUpdateWorldState(static_cast<uint32>(iArg(args, 0)), static_cast<uint32>(iArg(args, 1)),
                                         bArg(args, 2, false));
            return true;
        }
        if (name == "player.sendSpiritResurrect")
        {
            if (player->GetSession())
                player->GetSession()->SendSpiritResurrect();
            return true;
        }
        if (name == "player.setAcceptWhispers")
        {
            player->SetAcceptWhispers(bArg(args, 0, true));
            return true;
        }
        if (name == "player.setAtLoginFlag")
        {
            player->SetAtLoginFlag(static_cast<AtLoginFlags>(iArg(args, 0)));
            return true;
        }
        if (name == "player.setDrunkValue")
        {
            player->SetDrunkValue(static_cast<uint8>(iArg(args, 0)));
            return true;
        }
        if (name == "player.setFactionForRace")
        {
            uint8 const race = static_cast<uint8>(iArg(args, 0));
            if (!sChrRacesStore.LookupEntry(race))
            {
                out.emplace_back(false);
                return true;
            }
            player->SetFactionForRace(race);
            return true;
        }
        if (name == "player.setGameMaster")
        {
            player->SetGameMaster(bArg(args, 0, false));
            return true;
        }
        if (name == "player.setGender")
        {
            int64_t const gender = iArg(args, 0);
            if (gender != 0 && gender != 1)
                return false;
            player->SetGender(static_cast<Gender>(gender));
            return true;
        }
        if (name == "player.setGMChat")
        {
            player->SetGMChat(bArg(args, 0, false));
            return true;
        }
        if (name == "player.setGMVisible")
        {
            player->SetGMVisible(bArg(args, 0, true));
            return true;
        }
        if (name == "player.setPvPDeath")
        {
            player->SetPvPDeath(bArg(args, 0, false));
            return true;
        }
        if (name == "player.spawnBones")
        {
            player->SpawnCorpseBones();
            return true;
        }
        if (name == "player.killPlayer")
        {
            player->KillPlayer();
            return true;
        }
        if (name == "player.resetTalents")
        {
            bool const noCost = bArg(args, 0, false);
            int64 const cost = noCost ? 0 : static_cast<int64>(player->GetNextResetTalentsCost());
            if (!noCost && !player->HasEnoughMoney(cost))
            {
                out.emplace_back(false);
                return true;
            }
            // The core's flag marks an involuntary reset (sending TalentsInvoluntarilyReset) and does not charge.
            bool const ok = player->ResetTalents(false);
            if (ok)
            {
                if (!noCost)
                {
                    player->ModifyMoney(-cost);
                    player->IncreaseResetTalentsCostAndCounters(static_cast<uint32>(cost));
                }
                player->SendTalentsInfoData();
            }
            out.emplace_back(ok);
            return true;
        }
        if (name == "player.durabilityLoss" || name == "player.durabilityPointsLoss")
        {
            // The item is resolved through the receiver's own inventory: an item of another player that a script
            // holds a handle for is not this player's to damage.
            HandleData itemHandle;
            if (!args.empty() && asHandle(args[0], itemHandle))
                if (Item* item = player->GetItemByGuid(toGuid(itemHandle.guid)))
                {
                    if (name == "player.durabilityLoss")
                    {
                        double const pct = dArg(args, 1);
                        if (!std::isfinite(pct))
                            return false;
                        player->DurabilityLoss(item, std::clamp(pct, 0.0, 1.0));
                    }
                    else
                        player->DurabilityPointsLoss(item, static_cast<int32>(iArg(args, 1)));
                }
            return true;
        }
        if (name == "player.durabilityLossAll")
        {
            double const pct = dArg(args, 0);
            if (!std::isfinite(pct))
                return false;
            player->DurabilityLossAll(std::clamp(pct, 0.0, 1.0), bArg(args, 1, true));
            return true;
        }
        if (name == "player.durabilityPointLossForEquipSlot")
        {
            player->DurabilityPointLossForEquipSlot(static_cast<EquipmentSlots>(iArg(args, 0)));
            return true;
        }
        if (name == "player.durabilityPointsLossAll")
        {
            player->DurabilityPointsLossAll(static_cast<int32>(iArg(args, 0)), bArg(args, 1, true));
            return true;
        }
        if (name == "player.durabilityRepair")
        {
            player->DurabilityRepair(static_cast<uint16>(iArg(args, 0)), bArg(args, 1, true),
                                     static_cast<float>(dArg(args, 2, 1.0)));
            return true;
        }

        // ---- E7 long tail, round 3: remaining documented Player reads, writes and social ops ----
        if (name == "player.canTitanGrip")
        {
            bool can = false;
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                if (Item* item = findItem(h))
                    can = player->CanTitanGrip(item);
            out.emplace_back(can);
            return true;
        }
        if (name == "player.getComboPoints")
        {
            out.emplace_back(static_cast<int64_t>(std::max<int32>(0, player->GetPower(POWER_COMBO_POINTS))));
            return true;
        }
        if (name == "player.getComboTarget")
        {
            ObjectGuid const targetGuid = player->GetClassicComboTarget();
            if (!targetGuid.IsEmpty())
                if (Unit* target = ObjectAccessor::GetUnit(*player, targetGuid))
                    if (target->IsInWorld())
                        pushHandle(out, handleFor(target));
            return true;
        }
        if (name == "player.getGossipTextId")
        {
            uint32 textId = 0;
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                if (WorldObject* src = woOf(h))
                    textId = player->GetGossipTextId(src);
            out.emplace_back(static_cast<int64_t>(textId));
            return true;
        }
        if (name == "player.hasRecruited")
        {
            bool has = false;
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                if (Player* recruit = playerOf(h))
                    if (player->GetSession() && recruit->GetSession())
                    {
                        uint32 const acct = player->GetSession()->GetAccountId();
                        has = acct != 0 && recruit->GetSession()->GetRecruiterId() == acct;
                    }
            out.emplace_back(has);
            return true;
        }
        if (name == "player.isRecruited")
        {
            out.emplace_back(player->GetSession() && player->GetSession()->GetRecruiterId() != 0);
            return true;
        }
        if (name == "player.addComboPoints")
        {
            Unit* target = nullptr;
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                target = unitOf(h);
            int32 const count = static_cast<int32>(iArg(args, 1));
            if (target)
            {
                if (count > 0 && player->GetClassicComboTarget() != target->GetGUID())
                {
                    player->SetPower(POWER_COMBO_POINTS, 0);
                    player->SetClassicComboTarget(target->GetGUID());
                }
                if (count != 0 && (count > 0 || player->GetClassicComboTarget() == target->GetGUID()))
                    player->ModifyPower(POWER_COMBO_POINTS, count);
            }
            return true;
        }
        if (name == "player.clearComboPoints")
        {
            player->SetPower(POWER_COMBO_POINTS, 0);
            player->SetClassicComboTarget(ObjectGuid::Empty);
            return true;
        }
        if (name == "player.addTalent")
        {
            uint32 const spellId = static_cast<uint32>(iArg(args, 0));
            uint8 const spec = static_cast<uint8>(iArg(args, 1));
            bool const learning = bArg(args, 2, true);
            bool ok = false;
            if (spec < MAX_SPECIALIZATIONS)
                for (TalentEntry const* talent : sTalentStore)
                    if (talent && talent->SpellID == spellId)
                    {
                        ok = player->AddTalent(talent, spec, learning);
                        if (ok && spec == player->GetActiveTalentGroup())
                            player->SendTalentsInfoData();
                        break;
                    }
            out.emplace_back(ok);
            return true;
        }
        if (name == "player.advanceAllSkills")
        {
            uint32 const step = static_cast<uint32>(iArg(args, 0));
            if (step > 0)
                for (uint32 i = 0; i < PLAYER_MAX_SKILLS; ++i)
                    if (uint16 const skillId = player->GetSkillLineIdByPos(i))
                        player->UpdateSkillPro(skillId, 1000, step);
            return true;
        }
        if (name == "player.advanceSkill")
        {
            uint32 const skillId = static_cast<uint32>(iArg(args, 0));
            uint32 const step = static_cast<uint32>(iArg(args, 1));
            if (skillId <= std::numeric_limits<uint16>::max() && step > 0)
                player->UpdateSkillPro(static_cast<uint16>(skillId), 1000, step);
            return true;
        }
        if (name == "player.advanceSkillsToMax")
        {
            for (uint32 i = 0; i < PLAYER_MAX_SKILLS; ++i)
                if (uint16 const skillId = player->GetSkillLineIdByPos(i))
                    if (SkillLineEntry const* sl = sSkillLineStore.LookupEntry(skillId))
                        if (sl->CategoryID == SKILL_CATEGORY_WEAPON)
                        {
                            uint16 const cur = player->GetPureSkillValue(skillId);
                            uint16 const maxVal = player->GetPureMaxSkillValue(skillId);
                            if (cur < maxVal)
                                player->UpdateSkillPro(skillId, 1000, maxVal - cur);
                        }
            return true;
        }
        if (name == "player.bindToInstance")
        {
            if (InstanceMap* imap = player->GetMap()->ToInstanceMap())
                imap->CreateInstanceLockForPlayer(player);
            return true;
        }
        if (name == "player.unbindInstance" || name == "player.unbindAllInstances")
        {
            bool const single = name == "player.unbindInstance";
            uint32 const mapId = single ? static_cast<uint32>(iArg(args, 0)) : 0;
            uint32 const diffArg = single ? static_cast<uint32>(iArg(args, 1, 0)) : 0;
            fl::Guid const playerGuid = fromGuid(player->GetGUID());
            // ResetInstanceLocksForPlayer walks _instanceLocksByPlayer without locking, so run on the world thread.
            auto job = [=]()
            {
                Player* target = ObjectAccessor::FindPlayer(toGuid(playerGuid));
                if (!target)
                    return;
                if (single && target->GetMapId() == mapId)
                    return;
                Optional<uint32> mapOpt = single ? Optional<uint32>(mapId) : std::nullopt;
                Optional<Difficulty> diffOpt =
                    single && diffArg ? Optional<Difficulty>(static_cast<Difficulty>(diffArg)) : std::nullopt;
                std::vector<InstanceLock const*> reset, failed;
                sInstanceLockMgr.ResetInstanceLocksForPlayer(target->GetGUID(), mapOpt, diffOpt, &reset, &failed);
            };
            runSocialWrite("Instance unbind", job);
            return true;
        }
        if (name == "player.gossipSendPOI")
        {
            if (player->GetSession())
            {
                float const x = static_cast<float>(dArg(args, 0));
                float const y = static_cast<float>(dArg(args, 1));
                float z = player->GetMapHeight(x, y, player->GetPositionZ() + 30.0f, true, 100.0f);
                if (z <= INVALID_HEIGHT)
                    z = player->GetPositionZ();
                std::string const text = sArg(args, 5);
                WorldPackets::NPC::GossipPOI packet;
                packet.ID = 0;
                packet.Flags = static_cast<uint32>(iArg(args, 3));
                packet.Pos.Pos.Relocate(x, y, z);
                packet.Icon = static_cast<int32>(iArg(args, 2));
                packet.Importance = static_cast<int32>(iArg(args, 4));
                packet.WMOGroupID = 0;
                packet.Name = text;
                player->GetSession()->SendPacket(packet.Write());
            }
            return true;
        }
        if (name == "player.killedPlayerCredit")
        {
            player->KilledPlayerCredit(player->GetTarget());
            return true;
        }
        if (name == "player.killGOCredit")
        {
            player->KillCreditGO(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.leaveBattleground")
        {
            player->LeaveBattleground(bArg(args, 0, true));
            return true;
        }
        if (name == "player.mute")
        {
            if (WorldSession* s = player->GetSession())
            {
                uint32 const secs = static_cast<uint32>(iArg(args, 0));
                s->m_muteTime = secs ? GameTime::GetGameTime() + secs : 0;
            }
            return true;
        }
        if (name == "player.removedInsignia")
        {
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                if (Player* looter = playerOf(h))
                    player->RemovedInsignia(looter);
            return true;
        }
        if (name == "player.removeFromBattlegroundRaid")
        {
            fl::Guid const playerGuid = fromGuid(player->GetGUID());
            runSocialWrite("Battleground raid leave",
                           [playerGuid]
                           {
                               if (Player* target = ObjectAccessor::FindPlayer(toGuid(playerGuid)))
                                   target->RemoveFromBattlegroundOrBattlefieldRaid();
                           });
            return true;
        }
        if (name == "player.removeFromGroup")
        {
            fl::Guid const playerGuid = fromGuid(player->GetGUID());
            runSocialWrite("Group leave",
                           [playerGuid]
                           {
                               if (Player* target = ObjectAccessor::FindPlayer(toGuid(playerGuid)))
                                   if (target->GetGroup())
                                       target->RemoveFromGroup();
                           });
            return true;
        }
        if (name == "player.removePet")
        {
            if (Pet* pet = player->GetPet())
            {
                int64 const m = iArg(args, 0, -1);
                PetSaveMode mode = PET_SAVE_AS_DELETED;
                if (m == 0)
                    mode = PET_SAVE_AS_CURRENT;
                else if (m >= 1 && m <= 4)
                    mode = static_cast<PetSaveMode>(PET_SAVE_FIRST_STABLE_SLOT + (m - 1));
                player->RemovePet(pet, mode, bArg(args, 1, false));
            }
            return true;
        }
        if (name == "player.summonPet")
        {
            uint32 const entry = static_cast<uint32>(iArg(args, 0));
            if (!entry)
                return true;
            float const x = static_cast<float>(dArg(args, 1));
            float const y = static_cast<float>(dArg(args, 2));
            float const z = static_cast<float>(dArg(args, 3));
            float const o = static_cast<float>(dArg(args, 4));
            int64 const petType = iArg(args, 5);
            uint32 const despawnTime = static_cast<uint32>(iArg(args, 6));
            if (petType == SUMMON_PET)
                player->SummonPet(entry, {}, x, y, z, o, despawnTime);
            else if (petType == HUNTER_PET && player->GetPetGUID().IsEmpty())
            {
                if (Pet* pet = player->CreateTamedPetFrom(entry))
                {
                    pet->Relocate(x, y, z, o);
                    if (!pet->IsPositionValid())
                    {
                        delete pet;
                        return true;
                    }
                    pet->GetMap()->AddToMap(pet->ToCreature());
                    player->SetMinion(pet, true);
                    if (despawnTime > 0)
                        pet->SetDuration(despawnTime);
                    pet->SavePetToDB(PET_SAVE_AS_CURRENT);
                    player->PetSpellInitialize();
                }
            }
            return true;
        }
        if (name == "player.sendAddonMessage")
        {
            std::string const prefix = sArg(args, 0);
            std::string const msg = sArg(args, 1);
            int64 const ch = iArg(args, 2);
            Player* receiver = nullptr;
            if (HandleData h; args.size() > 3 && asHandle(args[3], h))
                receiver = playerOf(h);
            if (receiver && ch >= 0 && ch < MAX_CHAT_MSG_TYPE)
            {
                WorldPackets::Chat::Chat packet;
                packet.Initialize(static_cast<ChatMsg>(ch), LANG_ADDON, player, receiver, msg, 0, "", DEFAULT_LOCALE,
                                  prefix);
                receiver->SendDirectMessage(packet.Write());
            }
            return true;
        }
        if (name == "player.sendAuctionMenu")
        {
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                if (Unit* sender = unitOf(h))
                    if (player->GetSession())
                        player->GetSession()->SendAuctionHello(sender->GetGUID(), sender);
            return true;
        }
        if (name == "player.sendQuestTemplate")
        {
            if (player->PlayerTalkClass)
                if (Quest const* quest = sObjectMgr->GetQuestTemplate(static_cast<uint32>(iArg(args, 0))))
                    player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, player->GetGUID(), bArg(args, 1, true),
                                                                        false);
            return true;
        }
        if (name == "player.sendShowMailBox")
        {
            if (player->GetSession())
            {
                ObjectGuid target = player->GetGUID();
                if (!args.empty())
                {
                    ObjectGuid const parsed = guidArgAt(args, 0, LegacyGuid::Player);
                    if (!parsed.IsEmpty())
                        target = parsed;
                }
                player->GetSession()->SendShowMailBox(target);
            }
            return true;
        }
        if (name == "player.sendTabardVendorActivate")
        {
            if (HandleData h; !args.empty() && asHandle(args[0], h))
                if (WorldObject* sender = woOf(h))
                    if (player->GetSession())
                        player->GetSession()->SendTabardVendorActivate(sender->GetGUID(),
                                                                       static_cast<TabardVendorType>(0));
            return true;
        }
        if (name == "player.setKnownTitle" || name == "player.unsetKnownTitle")
        {
            if (CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(static_cast<uint32>(iArg(args, 0))))
                player->SetTitle(title, name == "player.unsetKnownTitle");
            return true;
        }
        if (name == "player.setMovement")
        {
            switch (iArg(args, 0))
            {
                case 1: player->SetControlled(true, UNIT_STATE_ROOT); break;
                case 2: player->SetControlled(false, UNIT_STATE_ROOT); break;
                case 3: player->SetWaterWalking(true); break;
                case 4: player->SetWaterWalking(false); break;
                default: break;
            }
            return true;
        }
        if (name == "player.setPlayerLock")
        {
            player->SetClientControl(player, !bArg(args, 0, true));
            return true;
        }
        if (name == "player.setQuestStatus")
        {
            int64 const status = iArg(args, 1);
            if (status >= 0 && status < MAX_QUEST_STATUS)
                player->SetQuestStatus(static_cast<uint32>(iArg(args, 0)), static_cast<QuestStatus>(status));
            return true;
        }
        if (name == "player.startTaxi")
        {
            player->ActivateTaxiPathTo(static_cast<uint32>(iArg(args, 0)));
            return true;
        }
        if (name == "player.groupCreate")
        {
            HandleData invitedHandle;
            if (args.empty() || !asHandle(args[0], invitedHandle))
                return true;
            HandleData const leaderHandle = self;
            auto job = [=]() -> std::optional<HandleData>
            {
                Player* leader = playerOf(leaderHandle);
                Player* invited = playerOf(invitedHandle);
                // A pending invite holds a raw pointer in the inviting group's invitee set; AddMember only clears the
                // player's side, so both participants must be free of one before a new group takes them.
                if (!leader || !invited || leader == invited || leader->GetGroup() || invited->GetGroup() ||
                    leader->GetGroupInvite() || invited->GetGroupInvite())
                    return std::nullopt;
                Group* group = new Group();
                if (!group->Create(leader))
                {
                    delete group;
                    return std::nullopt;
                }
                sGroupMgr->AddGroup(group);
                if (!group->AddMember(invited))
                {
                    group->Disband();
                    return std::nullopt;
                }
                HandleData gh;
                gh.kind = ObjKind::Group;
                gh.guid = fromGuid(group->GetGUID());
                return gh;
            };
            if (rules().placeForWorld() == AccessRules::Place::Direct && socialWriteBlocked())
            {
                runSocialWrite("Group create", [job] { (void)job(); });
                return true;
            }
            auto guarded = [this, job]() -> std::optional<HandleData>
            {
                struct Guard
                {
                    bool& flag;
                    ~Guard() { flag = false; }
                } reset{_socialWriteActive};
                _socialWriteActive = true;
                return job();
            };
            WorldCallResult how = WorldCallResult::Ran;
            std::optional<std::optional<HandleData>> r =
                callOnWorld<std::optional<HandleData>>(rules(), deferred(), guarded, &how);
            if (how == WorldCallResult::Dropped)
                log(fl::LogLevel::Warn, "ForeverLua: GroupCreate dropped: the world thread queue is full");
            if (r && *r)
                pushHandle(out, **r);
            return true;
        }
        if (name == "player.groupInvite")
        {
            HandleData invitedHandle;
            if (args.empty() || !asHandle(args[0], invitedHandle))
                return true;
            HandleData const inviterHandle = self;
            auto job = [=]() -> bool
            {
                Player* inviter = playerOf(inviterHandle);
                Player* invited = playerOf(invitedHandle);
                if (!inviter || !invited || inviter == invited || invited->GetGroup() || invited->GetGroupInvite())
                    return false;
                Group* group = inviter->GetGroup();
                if (!group)
                    group = inviter->GetGroupInvite();
                if (group)
                {
                    if ((!group->IsLeader(inviter->GetGUID()) && !group->IsAssistant(inviter->GetGUID())) ||
                        group->IsFull())
                        return false;
                }
                else
                {
                    group = new Group();
                    if (!group->AddLeaderInvite(inviter))
                    {
                        delete group;
                        return false;
                    }
                }
                if (!group->AddInvite(invited))
                {
                    if (!group->IsCreated())
                    {
                        group->RemoveAllInvites();
                        delete group;
                    }
                    return false;
                }
                WorldPackets::Party::PartyInvite partyInvite;
                partyInvite.Initialize(inviter, 0, true);
                invited->SendDirectMessage(partyInvite.Write());
                if (inviter->GetSession())
                    inviter->GetSession()->SendPartyResult(PARTY_OP_INVITE, invited->GetName(), ERR_PARTY_RESULT_OK);
                return true;
            };
            if (rules().placeForWorld() == AccessRules::Place::Direct && socialWriteBlocked())
            {
                runSocialWrite("Group invite", [job] { (void)job(); });
                return true;
            }
            auto guarded = [this, job]() -> bool
            {
                struct Guard
                {
                    bool& flag;
                    ~Guard() { flag = false; }
                } reset{_socialWriteActive};
                _socialWriteActive = true;
                return job();
            };
            WorldCallResult how = WorldCallResult::Ran;
            std::optional<bool> r = callOnWorld<bool>(rules(), deferred(), guarded, &how);
            if (how == WorldCallResult::Dropped)
                log(fl::LogLevel::Warn, "ForeverLua: GroupInvite dropped: the world thread queue is full");
            if (r)
                out.emplace_back(*r);
            return true;
        }
        if (name == "player.sendGuildInvite")
        {
            HandleData inviteeHandle;
            if (args.empty() || !asHandle(args[0], inviteeHandle))
                return true;
            HandleData const inviterHandle = self;
            runSocialWrite("Guild invite",
                           [inviterHandle, inviteeHandle]
                           {
                               Player* inviter = findPlayer(inviterHandle);
                               Player* invitee = findPlayer(inviteeHandle);
                               if (inviter && invitee && inviter->GetSession())
                                   if (Guild* guild = inviter->GetGuild())
                                       guild->HandleInviteMember(inviter->GetSession(), invitee->GetName());
                           });
            return true;
        }
        if (name == "player.setGuildRank")
        {
            uint8 const rank = static_cast<uint8>(iArg(args, 0));
            fl::Guid const playerGuid = fromGuid(player->GetGUID());
            runSocialWrite("Guild rank change",
                           [playerGuid, rank]
                           {
                               Player* target = ObjectAccessor::FindPlayer(toGuid(playerGuid));
                               if (!target)
                                   return;
                               if (Guild* guild = target->GetGuild())
                               {
                                   CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                                   if (guild->ChangeMemberRank(trans, target->GetGUID(), static_cast<GuildRankId>(rank)))
                                       CharacterDatabase.CommitTransaction(trans);
                               }
                           });
            return true;
        }
        if (name == "player.runCommand")
        {
            std::string const line = sArg(args, 0);
            if (line.empty())
                return true;
            fl::Guid const playerGuid = fromGuid(player->GetGUID());
            auto job = [playerGuid, line]
            {
                Player* target = ObjectAccessor::FindPlayer(toGuid(playerGuid));
                if (!target || !target->GetSession())
                    return;
                std::string cmd = (line[0] == '.' || line[0] == '!') ? line : ("." + line);
                ChatHandler(target->GetSession()).ParseCommands(cmd);
            };
            if (rules().placeForWorld() == AccessRules::Place::Direct)
                job();
            else if (!deferred().push(job))
                log(fl::LogLevel::Warn, "ForeverLua: Player:RunCommand dropped: the world thread queue is full");
            return true;
        }

        // ---- gossip (PlayerMenu) ----
        if (name == "gossip.addItem")
        {
            if (!player->PlayerTalkClass)
                return false;
            GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
            uint32 const icon = static_cast<uint32>(iArg(args, 0));
            std::string const msg = sArg(args, 1);
            uint32 const sender = static_cast<uint32>(iArg(args, 2));
            uint32 const intid = static_cast<uint32>(iArg(args, 3));
            bool const code = bArg(args, 4, false);
            std::string popup;
            asString(args.size() > 5 ? args[5] : EventValue(), popup);
            uint32 const money = static_cast<uint32>(iArg(args, 6, 0));
            int32 const optionId = static_cast<int32>(menu.GetMenuItemCount());
            menu.AddMenuItem(optionId, optionId, GossipOptionNpc::None, msg, LANG_UNIVERSAL, GossipOptionFlags::None,
                             {}, 0, 0, code, money, popup, {}, icon ? Optional<int32>(static_cast<int32>(icon)) : Optional<int32>(),
                             sender, intid);
            return true;
        }
        if (name == "gossip.sendMenu")
        {
            if (!player->PlayerTalkClass)
                return false;
            uint32 const npcText = static_cast<uint32>(iArg(args, 0));
            HandleData sender;
            if (args.size() < 2 || !asHandle(args[1], sender))
                return false;
            uint32 const menuId = static_cast<uint32>(iArg(args, 2, 0));
            player->PlayerTalkClass->GetGossipMenu().SetMenuId(menuId);
            player->PlayerTalkClass->SendGossipMenu(npcText, toGuid(sender.guid));
            return true;
        }
        if (name == "gossip.complete")
        {
            if (player->PlayerTalkClass)
                player->PlayerTalkClass->SendCloseGossip();
            return true;
        }
        if (name == "gossip.clear")
        {
            if (player->PlayerTalkClass)
                player->PlayerTalkClass->ClearMenus();
            return true;
        }
        if (name == "gossip.addQuests")
        {
            HandleData source;
            if (args.empty() || !asHandle(args[0], source))
                return false;
            if (WorldObject* src = woOf(source))
                player->PrepareGossipMenu(src, 0, true);
            return true;
        }
    }

    return false;
}

bool ForeverHost::guidInfo(fl::Guid const& g, uint32_t* entry, int* type)
{
    if (g.hi == 0)
    {
        // The legacy low-counter form: nothing decodes from it.
        if (type)
            *type = -1;
        if (entry)
            *entry = 0;
        return true;
    }
    ObjectGuid const guid = toGuid(g);
    HighGuid const high = guid.GetHigh();
    if (type)
        *type = static_cast<int>(high);
    if (entry)
    {
        bool const hasEntry = high == HighGuid::Creature || high == HighGuid::GameObject || high == HighGuid::Pet ||
                              high == HighGuid::Vehicle;
        *entry = hasEntry ? guid.GetEntry() : 0;
    }
    return true;
}

namespace
{
// A player's handle from its guid and the map record, never from the Player's own (other thread's) map pointer.
bool playerHandleFromRecord(fl::Guid const& guid, fl::HandleData& out)
{
    std::optional<MapKey> key = playerMaps().get(guid);
    if (!key)
        return false;
    out.kind = fl::ObjKind::Player;
    out.guid = guid;
    out.mapId = key->map;
    out.instanceId = key->instance;
    return true;
}
}  // namespace

// The bot API: the Battle.net account of the player's session, or 0 (GameAccess.h's `isBot` is unrelated).
bool ForeverHost::playerOwner(fl::HandleData const& h, uint64_t& owner)
{
    owner = 0;
    if (h.kind != fl::ObjKind::Player)
        return false;
    Player* player = findPlayer(h);
    if (!player || !player->GetSession())
        return false;
    // In memory, no login-DB query on a map or world thread: the session knows its Battle.net account, the same
    // identity the bot module memoises for a bot at login. A player with no session (offline) has no owner (0, as passed).
    owner = player->GetSession()->GetBattlenetAccountId();
    return owner != 0;
}

bool ForeverHost::playerByName(std::string const& name, fl::HandleData& out)
{
    Player* p = ObjectAccessor::FindPlayerByName(name);
    if (!p)
        return false;
    return playerHandleFromRecord(fromGuid(p->GetGUID()), out);
}

bool ForeverHost::playerByGuid(fl::Guid const& guid, fl::HandleData& out)
{
    fl::Guid g = guid;
    if (g.hi == 0)   // the legacy low-counter form of a player
        g = fromGuid(ObjectGuid::Create<HighGuid::Player>(static_cast<ObjectGuid::LowType>(guid.lo)));
    if (playerHandleFromRecord(g, out))
        return true;
    if (!worldThread() || !rules().mapsIdle())
        return false;
    Player* p = ObjectAccessor::FindPlayer(toGuid(g));   // not recorded yet (a login this tick): world thread only
    if (!p || !p->IsInWorld())
        return false;
    out = handleFor(p);
    return true;
}

std::vector<fl::HandleData> ForeverHost::playersInWorld(int team, bool onlyGm)
{
    // From the map records, not by walking the world's sessions (those belong to the world thread): each entry is
    // looked up by guid in the core's own thread-safe player table, only for the team/GM filters.
    std::vector<fl::HandleData> out;
    for (auto const& e : playerMaps().snapshot())
    {
        if (onlyGm || team == 0 || team == 1)
        {
            Player* p = ObjectAccessor::FindPlayer(toGuid(e.first));
            if (!p)
                continue;
            if (onlyGm && !p->IsGameMaster())
                continue;
            if (team == 0 && p->GetTeamId() != TEAM_ALLIANCE)
                continue;
            if (team == 1 && p->GetTeamId() != TEAM_HORDE)
                continue;
        }
        fl::HandleData h;
        h.kind = fl::ObjKind::Player;
        h.guid = e.first;
        h.mapId = e.second.map;
        h.instanceId = e.second.instance;
        out.push_back(h);
    }
    return out;
}

size_t ForeverHost::playerCount()
{
    return playerMaps().size();
}

void ForeverHost::sendWorldMessage(std::string const& msg)
{
    for (auto const& e : playerMaps().snapshot())
        if (Player* p = ObjectAccessor::FindPlayer(toGuid(e.first)))
            if (p->GetSession())
                ChatHandler(p->GetSession()).SendSysMessage(msg);
}

namespace
{
bool spawnNow(int spawnType, uint32_t entry, uint32_t mapId, uint32_t instanceId, float x, float y, float z, float o,
              bool save, uint32_t respawnOrDespawn, uint32_t phase, fl::HandleData& out)
{
    Map* map = findMap(mapId, instanceId);
    if (!map)
        return false;
    Position pos(x, y, z, o);
    if (spawnType == 1)
    {
        TempSummon* creature = map->SummonCreature(entry, pos, nullptr,
                                                   save ? 0ms : Milliseconds(respawnOrDespawn), nullptr);
        if (!creature)
            return false;
        (void)phase;   // per-phase spawns need the phasing API; E4
        if (save)
        {
            creature->SaveToDB(mapId, std::vector<Difficulty>{});
            creature->SetRespawnDelay(respawnOrDespawn);
        }
        out = handleFor(creature);
        return true;
    }
    if (spawnType == 2)
    {
        GameObject* go = GameObject::CreateGameObject(entry, map, pos, QuaternionData(), 255, GO_STATE_READY);
        if (!go)
            return false;
        map->AddToMap(go);
        if (respawnOrDespawn)
            go->SetRespawnTime(static_cast<int32>(respawnOrDespawn));
        if (save)
            go->SaveToDB(mapId, std::vector<Difficulty>{});
        out = handleFor(go);
        return true;
    }
    return false;
}
}  // namespace

bool ForeverHost::performSpawn(int spawnType, uint32_t entry, uint32_t mapId, uint32_t instanceId, float x, float y,
                               float z, float o, bool save, uint32_t respawnOrDespawn, uint32_t phase, fl::HandleData& out)
{
    // Adding an object to a map is that map's thread's work: on the target map's own thread, or on the world thread with
    // the maps idle, it runs now; from anywhere else it is queued to the world thread's next tick and the script gets
    // nil (the object is not there yet).
    if (rules().placeForMap(MapKey{mapId, instanceId}) == AccessRules::Place::Direct)
        return spawnNow(spawnType, entry, mapId, instanceId, x, y, z, o, save, respawnOrDespawn, phase, out);
    if (spawnType != 1 && spawnType != 2)
        return false;
    if (!deferred().push([=]
        {
            fl::HandleData ignored;
            spawnNow(spawnType, entry, mapId, instanceId, x, y, z, o, save, respawnOrDespawn, phase, ignored);
        }))
        log(fl::LogLevel::Warn, "ForeverLua: PerformIngameSpawn dropped: the world thread queue is full");
    return false;
}

void ForeverHost::runCommand(std::string const& line)
{
    // A command is the world thread's alone (it may reload, ban, shut down): from a map thread it is queued.
    auto job = [this, line]
    {
        std::vector<std::string> out;
        runGmLine(line, out);
    };
    if (rules().placeForWorld() == AccessRules::Place::Direct)
        job();
    else if (!deferred().push(job))
        log(fl::LogLevel::Warn, "ForeverLua: RunCommand dropped: the world thread queue is full");
}

std::vector<fl::HandleData> ForeverHost::objectsInRange(fl::HandleData const& self, int kind, float range,
                                                        uint32_t entry, int hostile, int dead)
{
    range = flh::clampSearchRange(range);
    std::vector<fl::HandleData> out;
    WorldObject* me = findObject(self);
    if (!me || !me->GetMap())
        return out;
    auto keep = [&](WorldObject* o) -> bool
    {
        if (!o || !o->IsInWorld())
            return false;
        if (me->GetDistance(o) > range)
            return false;
        if (hostile == 1 && !me->IsHostileTo(o))
            return false;
        if (hostile == 2 && !me->IsFriendlyTo(o))
            return false;
        if (Unit* u = o->ToUnit())
        {
            if (dead == 1 && !u->IsAlive())
                return false;
            if (dead == 2 && u->IsAlive())
                return false;
        }
        return true;
    };
    if (kind == 0)
    {
        for (MapReference const& ref : me->GetMap()->GetPlayers())
            if (Player* p = ref.GetSource())
                if (keep(p))
                    out.push_back(handleFor(p));
    }
    else if (kind == 1)
    {
        std::vector<Creature*> list;
        if (entry)
            me->GetCreatureListWithEntryInGrid(list, entry, range);
        else
            me->GetCreatureListWithOptionsInGrid(list, range, FindCreatureOptions());
        for (Creature* c : list)
            if (keep(c))
                out.push_back(handleFor(c));
    }
    else
    {
        std::vector<GameObject*> list;
        if (entry)
            me->GetGameObjectListWithEntryInGrid(list, entry, range);
        else
            me->GetGameObjectListWithOptionsInGrid(list, range, FindGameObjectOptions());
        for (GameObject* g : list)
            if (keep(g))
                out.push_back(handleFor(g));
    }
    std::sort(out.begin(), out.end(), [&](fl::HandleData const& a, fl::HandleData const& b)
    {
        WorldObject* oa = findObject(a);
        WorldObject* ob = findObject(b);
        float const da = oa ? me->GetDistance(oa) : 0.0f;
        float const db = ob ? me->GetDistance(ob) : 0.0f;
        return da < db;
    });
    return out;
}

std::string ForeverHost::itemLink(uint32_t entry)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!proto)
        return {};
    return "|cffffffff|Hitem:" + std::to_string(entry) + ":0:0:0:0:0:0:0|h[" + proto->GetName(LOCALE_enUS) + "]|h|r";
}

std::string ForeverHost::areaName(uint32_t id)
{
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(id))
        return std::string(area->AreaName[LOCALE_enUS]);
    return {};
}

std::string ForeverHost::coreVersion()
{
#ifdef TRINITY_FILEVERSION_STR
    return TRINITY_FILEVERSION_STR;
#else
    return "ForeverCore 1.60 (Classic)";
#endif
}

bool ForeverHost::mapExists(uint32_t mapId, uint32_t instanceId)
{
    if (!sMapStore.LookupEntry(mapId))
        return false;
    Map* map = findMap(mapId, instanceId);
    return map != nullptr || instanceId == 0;
}

bool ForeverHost::questExists(uint32_t questId) { return sObjectMgr->GetQuestTemplate(questId) != nullptr; }
bool ForeverHost::spellExists(uint32_t spellId) { return spellEntry(spellId) != nullptr; }

// E5: Player/Group/Guild:SendPacket. A send from a map thread goes through the deferred queue (the world thread performs
// it after the maps): a group or guild broadcast from one map state must not walk other maps' sessions there.
bool ForeverHost::sendPacket(fl::HandleData const& target, uint32_t opcode, std::vector<uint8_t> const& data,
                             bool ignorePlayersInBg, uint64_t ignore)
{
    if (target.kind == ObjKind::Creature || target.kind == ObjKind::GameObject || target.kind == ObjKind::Corpse)
    {
        WorldObject* wo = findObject(target);
        if (!wo)
            return false;
        WorldPacket packet(opcode, data.size());
        if (!data.empty())
            packet.append(data.data(), data.size());
        wo->SendMessageToSet(&packet, true);
        return true;
    }
    if (target.kind != ObjKind::Player && target.kind != ObjKind::Group && target.kind != ObjKind::Guild)
        return false;
    if (!worldThread())
    {
        return deferred().push([this, target, opcode, data, ignorePlayersInBg, ignore] {
            ForeverHost::sendPacket(target, opcode, data, ignorePlayersInBg, ignore);
        });
    }
    WorldPacket packet(opcode, data.size());
    if (!data.empty())
        packet.append(data.data(), data.size());
    switch (target.kind)
    {
        case ObjKind::Player:
        {
            Player* p = findPlayer(target);
            if (!p || !p->GetSession())
                return false;
            p->GetSession()->SendPacket(&packet);
            return true;
        }
        case ObjKind::Group:
        {
            Group* g = sGroupMgr->GetGroupByGUID(toGuid(target.guid));
            if (!g)
                return false;
            g->BroadcastPacket(&packet, ignorePlayersInBg, -1,
                               ignore ? toGuid(fl::Guid{0, ignore}) : ObjectGuid::Empty);
            return true;
        }
        case ObjKind::Guild:
        {
            Guild* g = sGuildMgr->GetGuildById(static_cast<uint32>(target.guid.lo));
            if (!g)
                return false;
            g->BroadcastPacket(&packet);
            return true;
        }
        default: return false;
    }
}

void ForeverHost::runSocialWrite(char const* what, std::function<void()> job)
{
    auto guarded = [this, job = std::move(job)]
    {
        struct Guard
        {
            bool& flag;
            ~Guard() { flag = false; }
        } reset{_socialWriteActive};
        _socialWriteActive = true;
        job();
    };
    // The flags are the world thread's: a map thread queues without reading them. On the world thread a change runs now
    // unless a script hook of a change (or of a core social action) already running asked for it: that one waits for the
    // next tick, after the core operation that is still unwinding has returned.
    bool queue = rules().placeForWorld() != AccessRules::Place::Direct;
    if (!queue)
        queue = socialWriteBlocked();
    if (queue)
    {
        if (!deferred().push(guarded))
            log(fl::LogLevel::Warn, std::string("ForeverLua: ") + what +
                                        " dropped: the world thread queue is full");
        return;
    }
    guarded();
}

bool ForeverHost::sendPacketToRanked(fl::HandleData const& target, uint32_t opcode, std::vector<uint8_t> const& data,
                                     uint32_t rank)
{
    if (target.kind != ObjKind::Guild)
        return false;
    if (!worldThread())
    {
        return deferred().push([this, target, opcode, data, rank] {
            ForeverHost::sendPacketToRanked(target, opcode, data, rank);
        });
    }
    Guild* guild = sGuildMgr->GetGuildById(static_cast<uint32>(target.guid.lo));
    if (!guild)
        return false;
    WorldPacket packet(opcode, data.size());
    if (!data.empty())
        packet.append(data.data(), data.size());
    guild->BroadcastPacketToRank(&packet, static_cast<GuildRankId>(rank));
    return true;
}

bool ForeverHost::rawQuery(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes,
                           fl::DbRows& out, std::string& error)
{
    if (!_db)
        _db = std::make_unique<LuaDb>(_dbTimeoutMs);
    return _db->query(db, sql, maxRows, maxBytes, out, error);
}

bool ForeverHost::rawExecute(std::string const& db, std::string const& sql)
{
    if (!_db)
        _db = std::make_unique<LuaDb>(_dbTimeoutMs);
    return _db->execute(db, sql);
}

void ForeverHost::requestReload() { _reloadRequested.store(true, std::memory_order_release); }

// ---------------------------------------------------------------------------------------------------------------
// E2 globals

bool ForeverHost::guildByName(std::string const& name, fl::HandleData& out)
{
    Guild* guild = sGuildMgr->GetGuildByName(name);
    if (!guild)
        return false;
    out.kind = ObjKind::Guild;
    out.guid.lo = guild->GetId();
    return true;
}

bool ForeverHost::guildByLeaderGuid(fl::Guid const& leader, fl::HandleData& out)
{
    Guild* guild = sGuildMgr->GetGuildByLeader(toGuid(leader));
    if (!guild)
        return false;
    out.kind = ObjKind::Guild;
    out.guid.lo = guild->GetId();
    return true;
}

namespace
{
// A world-only action with no result: runs now on the world thread (maps idle), else on its next tick.
void worldAction(ForeverHost& host, char const* what, std::function<void()> job)
{
    if (runOnWorld(rules(), deferred(), std::move(job)) == WorldCallResult::Dropped)
        host.log(fl::LogLevel::Warn, std::string("ForeverLua: ") + what + " dropped: the world thread queue is full");
}

int banNow(int mode, std::string const& nameOrIp, uint32_t durationSecs, std::string const& reason,
           std::string const& who)
{
    switch (mode)
    {
        case 0: return static_cast<int>(sWorld->BanAccount(BAN_ACCOUNT, nameOrIp, durationSecs, reason, who));
        case 1: return static_cast<int>(sWorld->BanCharacter(nameOrIp, std::to_string(durationSecs) + "s", reason, who));
        case 2: return static_cast<int>(sWorld->BanAccount(BAN_IP, nameOrIp, durationSecs, reason, who));
        default: return -1;
    }
}

std::vector<uint32_t> sendMailNow(std::string const& subject, std::string const& text, uint32_t receiverLow,
                                  uint32_t senderLow, uint32_t stationery, uint32_t delay, uint32_t money, uint32_t cod,
                                  std::vector<std::pair<uint32_t, uint32_t>> const& items)
{
    std::vector<uint32_t> guids;
    if (subject.size() > 200)
        return guids;
    // A receiver that does not exist would leave the items of the mail behind as orphan rows.
    if (!sCharacterCache->HasCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(receiverLow)))
        return guids;
    MailSender sender(MAIL_NORMAL, senderLow, static_cast<MailStationery>(stationery));
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    MailDraft draft(subject, text);
    for (auto const& pair : items)
    {
        Item* item = Item::CreateItem(pair.first, pair.second, ItemContext::NONE);
        if (!item)
            continue;
        // Saved with the draft: an item whose mail fails is deleted with the transaction.
        item->SaveToDB(trans);
        guids.push_back(static_cast<uint32_t>(item->GetGUID().GetCounter()));
        draft.AddItem(item);
    }
    if (money)
        draft.AddMoney(money);
    if (cod)
        draft.AddCOD(cod);
    draft.SendMailTo(trans, MailReceiver(receiverLow), sender, MAIL_CHECK_MASK_NOT_RETURNABLE, delay);
    CharacterDatabase.CommitTransaction(trans);
    return guids;
}

void addVendorItemNow(uint32_t entry, uint32_t item, int32_t maxCount, uint32_t incrTime, uint32_t extendedCost)
{
    VendorItem v;
    v.item = item;
    v.maxcount = maxCount < 0 ? 0 : static_cast<uint32>(maxCount);
    v.incrtime = incrTime;
    v.ExtendedCost = extendedCost;
    v.Type = 0;
    if (!sObjectMgr->IsVendorItemValid(entry, v))   // the vendor, the item and the extended cost must exist
        return;
    sObjectMgr->AddVendorItem(entry, v, true);
}

void vendorRemoveAllNow(uint32_t entry)
{
    VendorItemData const* list = sObjectMgr->GetNpcVendorItemList(entry);
    if (!list)
        return;
    std::vector<uint32> items;
    for (VendorItem const& v : list->m_items)
        items.push_back(v.item);
    for (uint32 item : items)
        sObjectMgr->RemoveVendorItem(entry, item, 0, true);
}
}  // namespace

// Every call below that changes world state (SaveAllPlayers, Kick, Ban, SendMail, vendors, game events) is the world
// thread's alone: from a map-thread handler it is queued (WorldCall.h) and runs on the world thread's next tick, so the
// script gets no result from Ban/SendMail then (nil), as the docs allow for "the core decided nothing".
void ForeverHost::saveAllPlayers()
{
    worldAction(*this, "SaveAllPlayers", [] { ObjectAccessor::SaveAllPlayers(); });
}

void ForeverHost::kickPlayer(fl::HandleData const& player)
{
    worldAction(*this, "Kick", [player]
    {
        if (Player* p = findPlayer(player))
            if (p->GetSession())
                p->GetSession()->KickPlayer("ForeverLua script");
    });
}

int ForeverHost::banPlayer(int mode, std::string const& nameOrIp, uint32_t durationSecs, std::string const& reason,
                           std::string const& who)
{
    WorldCallResult how = WorldCallResult::Ran;
    std::optional<int> r = callOnWorld<int>(
        rules(), deferred(), [=] { return banNow(mode, nameOrIp, durationSecs, reason, who); }, &how);
    if (how == WorldCallResult::Dropped)
        log(fl::LogLevel::Warn, "ForeverLua: Ban dropped: the world thread queue is full");
    return r ? *r : -1;
}

std::vector<uint32_t> ForeverHost::sendMail(std::string const& subject, std::string const& text, uint32_t receiverLow,
                                            uint32_t senderLow, uint32_t stationery, uint32_t delay, uint32_t money,
                                            uint32_t cod, std::vector<std::pair<uint32_t, uint32_t>> const& items)
{
    WorldCallResult how = WorldCallResult::Ran;
    std::optional<std::vector<uint32_t>> r = callOnWorld<std::vector<uint32_t>>(
        rules(), deferred(),
        [=] { return sendMailNow(subject, text, receiverLow, senderLow, stationery, delay, money, cod, items); }, &how);
    if (how == WorldCallResult::Dropped)
        log(fl::LogLevel::Warn, "ForeverLua: SendMail dropped: the world thread queue is full");
    return r ? *r : std::vector<uint32_t>{};
}

void ForeverHost::addVendorItem(uint32_t entry, uint32_t item, int32_t maxCount, uint32_t incrTime,
                                uint32_t extendedCost)
{
    worldAction(*this, "AddVendorItem",
                [=] { addVendorItemNow(entry, item, maxCount, incrTime, extendedCost); });
}

void ForeverHost::vendorRemoveItem(uint32_t entry, uint32_t item)
{
    worldAction(*this, "VendorRemoveItem", [=] { sObjectMgr->RemoveVendorItem(entry, item, 0, true); });
}

void ForeverHost::vendorRemoveAllItems(uint32_t entry)
{
    worldAction(*this, "VendorRemoveAllItems", [=] { vendorRemoveAllNow(entry); });
}

std::vector<uint32_t> ForeverHost::activeGameEvents()
{
    std::vector<uint32_t> out;
    for (uint16 id : sGameEventMgr->GetActiveEventList())
        out.push_back(id);
    return out;
}

bool ForeverHost::isGameEventActive(uint32_t eventId)
{
    return eventId <= 0xFFFF && sGameEventMgr->IsActiveEvent(static_cast<uint16>(eventId));
}

void ForeverHost::startGameEvent(uint32_t eventId, bool force)
{
    if (eventId <= 0xFFFF)
        worldAction(*this, "StartGameEvent", [=] { sGameEventMgr->StartEvent(static_cast<uint16>(eventId), force); });
}

void ForeverHost::stopGameEvent(uint32_t eventId, bool force)
{
    if (eventId <= 0xFFFF)
        worldAction(*this, "StopGameEvent", [=] { sGameEventMgr->StopEvent(static_cast<uint16>(eventId), force); });
}

bool ForeverHost::worldOnlyReadOk() { return rules().placeForWorld() == AccessRules::Place::Direct; }

bool ForeverHost::mapIsInstanced(uint32_t mapId)
{
    MapEntry const* entry = sMapStore.LookupEntry(mapId);
    return entry && entry->IsDungeon() && !entry->IsBattlegroundOrArena();
}

bool ForeverHost::commandNameReserved(std::string const& name) { return _coreRoots.reserves(name); }

// ---------------------------------------------------------------------------------------------------------------
// E3: the module's own connections, the asynchronous queries, ForeverData and the command map

flh::LuaDb& ForeverHost::db()
{
    if (!_db)
        _db = std::make_unique<LuaDb>(_dbTimeoutMs);
    return *_db;
}

bool ForeverHost::rawQueryAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes,
                                uint64_t id, std::string& error)
{
    return this->db().queryAsync(db, sql, maxRows, maxBytes, id, error);
}

bool ForeverHost::takeRawQueryResult(uint64_t& id, fl::DbRows& out, std::string& error)
{
    return db().takeAsyncResult(id, out, error);
}

void ForeverHost::cancelRawQuery(uint64_t id) { db().cancelAsync(id); }

void ForeverHost::commandsChanged()
{
    // The 32 pre-made Lua slots in forever_lua_commands changed. The core's command map must not be rebuilt now: this can
    // run inside a command (`.lua reload`, a Lua command that calls RegisterCommand) and the core still reads its command
    // node after the invoker returns. The world script applies the change from its update (applyCommandMapChange).
    _cmdGate.markChanged();
}

bool ForeverHost::applyCommandMapChange()
{
    return _cmdGate.apply([] { Trinity::ChatCommands::InvalidateCommandMap(); });
}

bool ForeverHost::dataLoad(std::vector<fl::DataRow>& out, std::string& error)
{
    fl::DbRows rows;
    // The module owns the table (spec E3): created here, in the characters database.
    if (!db().query("characters", dataTableDdl(), 0, 0, rows, error))
    {
        error = "ForeverData table: " + error;
        return false;
    }
    rows = fl::DbRows{};
    if (!db().query("characters", dataColumnsSql(), 16, 1u << 16, rows, error))
    {
        error = "ForeverData table check: " + error;
        return false;
    }
    std::vector<std::pair<std::string, std::string>> columns;
    for (auto const& row : rows.rows)
        if (row.size() >= 2)
        {
            auto const* name = std::get_if<std::string>(&row[0]);
            auto const* type = std::get_if<std::string>(&row[1]);
            if (name && type)
                columns.emplace_back(*name, *type);
        }
    if (dataTableNeedsMigration(columns))
    {
        // An older table has VARCHAR keys (case-insensitive, padded): the keys become byte strings.
        log(fl::LogLevel::Warn, "ForeverLua: migrating forever_lua_data to binary key columns");
        rows = fl::DbRows{};
        if (!db().query("characters", dataMigrateSql(), 0, 0, rows, error))
        {
            error = "ForeverData table migration: " + error;
            return false;
        }
    }
    rows = fl::DbRows{};
    // One row more than the cap tells "exactly the cap" from "more than the cap": a larger table is refused, never cut.
    size_t const cap = fl::Engine::kMaxDataKeys;
    size_t const byteCap = fl::Engine::kMaxDataBytes + (16u << 20);
    if (!db().query("characters", dataSelectAllSql(), cap + 1, byteCap, rows, error))
    {
        error = "ForeverData load: " + error;
        return false;
    }
    if (rows.tooLarge || rows.rows.size() > cap)
    {
        error = "the table holds more than " + std::to_string(cap) + " rows or " + std::to_string(byteCap >> 20) +
                " MB; it is not loaded and not changed (raise the cap or clean the table)";
        return false;
    }
    for (auto const& row : rows.rows)
    {
        if (row.size() < 3)
            continue;
        auto const* script = std::get_if<std::string>(&row[0]);
        auto const* key = std::get_if<std::string>(&row[1]);
        auto const* value = std::get_if<std::string>(&row[2]);
        if (!script || !key || !value)
            continue;
        fl::DataRow r;
        r.script = *script;
        r.key = *key;
        r.value = *value;
        out.push_back(std::move(r));
    }
    return true;
}

bool ForeverHost::dataWrite(fl::DataWrite const& w)
{
    std::string const sql = w.erase ? dataEraseSql(w.script, w.key) : dataUpsertSql(w.script, w.key, w.value);
    return db().executeConfirmed("characters", sql, w.token);
}

void ForeverHost::takeDataWrites(std::vector<fl::DataWriteResult>& out)
{
    if (_db)
        _db->takeConfirmed(out);
}

fl::Guid ForeverHost::buildGuid(fl::ObjKind kind, uint64_t low, uint32_t entry, uint16_t mapId)
{
    auto const counter = static_cast<ObjectGuid::LowType>(low);
    switch (kind)
    {
        case ObjKind::Player: return fromGuid(ObjectGuid::Create<HighGuid::Player>(counter));
        case ObjKind::Item: return fromGuid(ObjectGuid::Create<HighGuid::Item>(counter));
        case ObjKind::Creature: return fromGuid(ObjectGuid::Create<HighGuid::Creature>(mapId, entry, counter));
        case ObjKind::GameObject: return fromGuid(ObjectGuid::Create<HighGuid::GameObject>(mapId, entry, counter));
        default: return fl::Guid{};
    }
}

uint32_t ForeverHost::realmId() { return sRealmList->GetCurrentRealmId().Realm; }

bool ForeverHost::isInventoryPos(int kind, uint8_t bag, uint8_t slot)
{
    switch (kind)
    {
        case 0: return Player::IsBagPos(static_cast<uint16>((static_cast<uint16>(bag) << 8) | slot));
        case 1: return Player::IsBankPos(bag, slot);
        case 2: return Player::IsEquipmentPos(bag, slot);
        case 3: return Player::IsInventoryPos(bag, slot);
        default: return false;
    }
}
}  // namespace flh
