# Checks cmake/DetectCoreHooks.cmake on a ScriptMgr.h with the notify-only C1-C4 declarations of PRs Karrz1/TrinityCore
# #375-#379 and on headers without them (the macros must be recomputed, not left over from a previous call). The match is
# on class and method names only: renamed parameters, other spacing and another return type still count, a hook named
# only in a comment or a non-virtual dispatcher of the same name does not.
include(${SRC}/cmake/DetectCoreHooks.cmake)

file(MAKE_DIRECTORY ${OUT})

macro(expect_hooks LABEL C1 C1E C2 C2U GOSSIP ITEMUSE CMD MASK)
    if(NOT FL_CORE_HOOK_C1 STREQUAL "${C1}" OR NOT FL_CORE_HOOK_C1_EVENTS STREQUAL "${C1E}" OR
       NOT FL_CORE_HOOK_C2 STREQUAL "${C2}" OR NOT FL_CORE_HOOK_C2_USE STREQUAL "${C2U}" OR
       NOT FL_CORE_HOOK_C3_GOSSIP STREQUAL "${GOSSIP}" OR NOT FL_CORE_HOOK_C3_ITEM_USE STREQUAL "${ITEMUSE}" OR
       NOT FL_CORE_HOOK_C4 STREQUAL "${CMD}" OR NOT FL_CORE_HOOK_MASK STREQUAL "${MASK}")
        message(FATAL_ERROR "${LABEL}: C1=${FL_CORE_HOOK_C1} C1e=${FL_CORE_HOOK_C1_EVENTS} C2=${FL_CORE_HOOK_C2} "
                "C2u=${FL_CORE_HOOK_C2_USE} C3g=${FL_CORE_HOOK_C3_GOSSIP} C3i=${FL_CORE_HOOK_C3_ITEM_USE} "
                "C4=${FL_CORE_HOOK_C4} mask=${FL_CORE_HOOK_MASK}; expected ${C1} ${C1E} ${C2} ${C2U} ${GOSSIP} ${ITEMUSE} "
                "${CMD} ${MASK}")
    endif()
endmacro()

# A dispatcher in ScriptMgr has the same method names as the hooks but is not virtual and is not a hook.
set(DISPATCH "
class TC_GAME_API ScriptMgr
{ public: bool OnItemUse(Player* player, Item* item, SpellCastTargets const& targets, ObjectGuid castId); };")

set(HOOKED "${OUT}/hooked/ScriptMgr.h")
file(WRITE ${HOOKED} "
class TC_GAME_API AllCreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureAddWorld(Creature* creature);
    virtual void OnCreatureRemoveWorld(Creature* creature);
    virtual void OnCreatureEngage(Creature* creature, Unit* target);
    virtual void OnCreatureLeaveCombat(Creature* creature);
    virtual void OnCreatureJustDied(Creature* creature, Unit* killer);
    virtual void OnCreatureAppear(Creature* creature);
};
class TC_GAME_API AllGameObjectScript : public ScriptObject
{ public:
    virtual void OnGameObjectAddWorld(GameObject* go);
    virtual void OnGameObjectRemoveWorld(GameObject* go);
    virtual void OnGameObjectUse(GameObject* go, Player* player);
};
class TC_GAME_API PlayerScript : public ScriptObject
{ public:
    virtual void OnGossipSelect(Player* player, ObjectGuid source, uint32 menuId, uint32 listId, std::string const& code);
    virtual bool OnItemUse(Player* player, Item* item, SpellCastTargets const& targets, ObjectGuid castId);
    virtual bool OnCommand(Player* player, std::string_view command);
};${DISPATCH}")
forever_lua_detect_core_hooks("${HOOKED}")
expect_hooks("every hook" 1 1 1 1 1 1 1 127)

# The same declarations with every parameter renamed, other spacing and return types: still detected.
set(RENAMED "${OUT}/renamed/ScriptMgr.h")
file(WRITE ${RENAMED} "
class  TC_GAME_API  AllCreatureScript : public ScriptObject
{ public:
    virtual void
      OnCreatureAddWorld (Creature* c);
    virtual void OnCreatureRemoveWorld(Creature *cr);
    virtual   void OnCreatureEngage(Creature* mob, Unit* victim);
    virtual void OnCreatureLeaveCombat( Creature * mob );
    virtual void OnCreatureJustDied(Creature* mob, Unit* who);
    virtual void OnCreatureAppear(Creature* mob);
};
class TC_GAME_API AllGameObjectScript : public ScriptObject
{ public:
    virtual void OnGameObjectAddWorld(GameObject* gameobject);
    virtual void OnGameObjectRemoveWorld(GameObject* gameobject);
    virtual void OnGameObjectUse(GameObject* gameobject, Player* user);
};
class TC_GAME_API PlayerScript : public ScriptObject
{ public:
    virtual void OnGossipSelect(Player* p, ObjectGuid src, uint32 menu, uint32 index, std::string const& text);
    virtual bool OnItemUse(Player* p, Item* i, SpellCastTargets const& t, ObjectGuid id);
    virtual bool OnCommand(Player* p, std::string_view text);
};")
forever_lua_detect_core_hooks("${RENAMED}")
expect_hooks("renamed parameters" 1 1 1 1 1 1 1 127)

# Only add/remove (no combat or use notifications): the C1/C2 base hooks without the extras.
set(BASEONLY "${OUT}/baseonly/ScriptMgr.h")
file(WRITE ${BASEONLY} "
class TC_GAME_API AllCreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureAddWorld(Creature* creature);
    virtual void OnCreatureRemoveWorld(Creature* creature);
    virtual void OnCreatureEngage(Creature* creature, Unit* target);
};
class TC_GAME_API AllGameObjectScript : public ScriptObject
{ public:
    virtual void OnGameObjectAddWorld(GameObject* go);
    virtual void OnGameObjectRemoveWorld(GameObject* go);
};")
forever_lua_detect_core_hooks("${BASEONLY}")
expect_hooks("add/remove only" 1 0 1 0 0 0 0 3)

# The header the pushed core branches really carry (AllCreatureScript + AllGameObjectScript, no PlayerScript hooks):
# C1, its notifications, C2 and its use: bits 1+2+32+64.
set(REAL "${OUT}/real/ScriptMgr.h")
file(WRITE ${REAL} "
class TC_GAME_API AllCreatureScript : public ScriptObject
{
    protected:
        explicit AllCreatureScript(char const* name) noexcept;
    public:
        ~AllCreatureScript();
        // Called when a creature (pets included) is added to the world.
        virtual void OnCreatureAddWorld(Creature* creature);
        virtual void OnCreatureRemoveWorld(Creature* creature);
        virtual void OnCreatureEngage(Creature* creature, Unit* target);
        virtual void OnCreatureLeaveCombat(Creature* creature);
        virtual void OnCreatureJustDied(Creature* creature, Unit* killer);
        virtual void OnCreatureAppear(Creature* creature);
};
class TC_GAME_API AllGameObjectScript : public ScriptObject
{
    public:
        virtual void OnGameObjectAddWorld(GameObject* go);
        virtual void OnGameObjectRemoveWorld(GameObject* go);
        virtual void OnGameObjectUse(GameObject* go, Player* player);
};
class TC_GAME_API ScriptMgr
{ public: void OnCreatureAppear(Creature* creature); void OnCreatureLeaveCombat(Creature* creature); };")
forever_lua_detect_core_hooks("${REAL}")
expect_hooks("the pushed core branches" 1 1 1 1 0 0 0 99)

# The first E6 draft's notification names (Evade/Reset) are not hooks of the real core: the notifications stay off.
set(OLDNAMES "${OUT}/oldnames/ScriptMgr.h")
file(WRITE ${OLDNAMES} "
class TC_GAME_API AllCreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureAddWorld(Creature* creature);
    virtual void OnCreatureRemoveWorld(Creature* creature);
    virtual void OnCreatureEngage(Creature* creature, Unit* target);
    virtual void OnCreatureEvade(Creature* creature);
    virtual void OnCreatureJustDied(Creature* creature, Unit* killer);
    virtual void OnCreatureReset(Creature* creature);
};")
forever_lua_detect_core_hooks("${OLDNAMES}")
expect_hooks("old names" 1 0 0 0 0 0 0 1)

# The same method names declared ONLY in unrelated classes (and a non-virtual dispatcher) enable nothing: the scan is scoped
# to the AllCreatureScript / AllGameObjectScript / PlayerScript bodies.
set(CROSS "${OUT}/cross/ScriptMgr.h")
file(WRITE ${CROSS} "
class TC_GAME_API CreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureAddWorld(Creature* creature);
    virtual void OnCreatureRemoveWorld(Creature* creature);
    virtual void OnCreatureEngage(Creature* creature, Unit* target);
    virtual void OnCreatureLeaveCombat(Creature* creature);
    virtual void OnCreatureJustDied(Creature* creature, Unit* killer);
    virtual void OnCreatureAppear(Creature* creature);
};
class TC_GAME_API GameObjectScript : public ScriptObject
{ public:
    virtual void OnGameObjectAddWorld(GameObject* go);
    virtual void OnGameObjectRemoveWorld(GameObject* go);
    virtual void OnGameObjectUse(GameObject* go, Player* player);
};
class TC_GAME_API ItemScript : public ScriptObject
{ public:
    virtual void OnGossipSelect(Player* player, ObjectGuid source, uint32 menuId, uint32 listId, std::string const& code);
    virtual bool OnItemUse(Player* player, Item* item, SpellCastTargets const& targets, ObjectGuid castId);
    virtual bool OnCommand(Player* player, std::string_view command);
};
class TC_GAME_API AllCreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureAddWorld(Creature* creature);
};
class TC_GAME_API PlayerScript : public ScriptObject
{ public: virtual void OnSpellCast(Player* player, Spell* spell, bool skipCheck); };${DISPATCH}")
forever_lua_detect_core_hooks("${CROSS}")
expect_hooks("same names in unrelated classes" 0 0 0 0 0 0 0 0)

# The methods split over classes: each is found only in its own class body, nested braces inside a body do not end it.
set(NESTED "${OUT}/nested/ScriptMgr.h")
file(WRITE ${NESTED} "
class TC_GAME_API AllCreatureScript : public ScriptObject
{ public:
    struct Helper { int x; };
    virtual void OnCreatureAddWorld(Creature* creature);
    virtual void OnCreatureRemoveWorld(Creature* creature);
};
class TC_GAME_API AllGameObjectScript : public ScriptObject
{ public:
    virtual void OnGameObjectAddWorld(GameObject* go);
    virtual void OnGameObjectRemoveWorld(GameObject* go);
};
class TC_GAME_API CreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureEngage(Creature* creature, Unit* target);
    virtual void OnCreatureLeaveCombat(Creature* creature);
    virtual void OnCreatureJustDied(Creature* creature, Unit* killer);
    virtual void OnCreatureAppear(Creature* creature);
};")
forever_lua_detect_core_hooks("${NESTED}")
expect_hooks("events in another class, nested struct" 1 0 1 0 0 0 0 3)

# The old AI-replacement hook is not a hook this module uses, and a hook named only in a comment is not a hook.
set(STALE "${OUT}/stale/ScriptMgr.h")
file(WRITE ${STALE} "
// virtual void OnCreatureAddWorld(Creature* creature); virtual void OnCreatureRemoveWorld(Creature* creature);
/* virtual bool OnCommand(Player* player, std::string_view command); */
class TC_GAME_API AllCreatureScript : public ScriptObject
{ public:
    virtual void OnCreatureAICreate(Creature* creature, CreatureAI*& ai);
};
class TC_GAME_API PlayerScript : public ScriptObject
{ public: virtual void OnSpellCast(Player* player, Spell* spell, bool skipCheck); };${DISPATCH}")
forever_lua_detect_core_hooks("${STALE}")
expect_hooks("stale and commented" 0 0 0 0 0 0 0 0)

set(BARE "${OUT}/bare/ScriptMgr.h")
file(WRITE ${BARE} "
class TC_GAME_API PlayerScript : public ScriptObject
{ public: virtual void OnSpellCast(Player* player, Spell* spell, bool skipCheck); };")
forever_lua_detect_core_hooks("${BARE}")
expect_hooks("bare header" 0 0 0 0 0 0 0 0)

forever_lua_detect_core_hooks("${OUT}/missing/ScriptMgr.h")
expect_hooks("missing header" 0 0 0 0 0 0 0 0)
message(STATUS "core hook detection: ok")
