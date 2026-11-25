---@meta

---@enum ServerEvents
ServerEvents = {
    NETWORK_START = 1,
    NETWORK_STOP = 2,
    SOCKET_OPEN = 3,
    SOCKET_CLOSE = 4,
    PACKET_RECEIVE = 5,
    ---the spec names no driving hook (section 3: none); the engine registers it and logs nothing
    PACKET_RECEIVE_UNKNOWN = 6,
    PACKET_SEND = 7,
    OPEN_STATE_CHANGE = 8,
    CONFIG_LOAD = 9,
    SHUTDOWN_INIT = 11,
    SHUTDOWN_CANCEL = 12,
    UPDATE = 13,
    STARTUP = 14,
    SHUTDOWN = 15,
    LUA_STATE_CLOSE = 16,
    ---world (continent) maps only; dungeons and raids need C11
    MAP_CREATE = 17,
    ---world (continent) maps only; dungeons and raids need C11
    MAP_DESTROY = 18,
    GRID_LOAD = 19,
    GRID_UNLOAD = 20,
    ---world (continent) maps only; dungeons and raids need C11
    MAP_PLAYER_ENTER = 21,
    ---world (continent) maps only; dungeons and raids need C11
    MAP_PLAYER_LEAVE = 22,
    ---world (continent) maps only; dungeons and raids need C11
    MAP_UPDATE = 23,
    ---@deprecated needs core hook C8
    TRIGGER_ON_TRIGGER = 24,
    WEATHER_CHANGE = 25,
    AUCTION_ADD = 26,
    AUCTION_REMOVE = 27,
    AUCTION_SUCCESSFUL = 28,
    AUCTION_EXPIRE = 29,
    ADDON_ON_MESSAGE = 30,
    ---needs core hook C1; the engine names its interim ScriptName path
    DELETE_CREATURE = 31,
    ---needs core hook C2; the engine names its interim ScriptName path
    DELETE_GAMEOBJECT = 32,
    LUA_STATE_OPEN = 33,
    ---@deprecated needs core hook C7
    GAME_EVENT_START = 34,
    ---@deprecated needs core hook C7
    GAME_EVENT_STOP = 35,
}

---@enum PlayerEvents
PlayerEvents = {
    CHARACTER_CREATE = 1,
    CHARACTER_DELETE = 2,
    LOGIN = 3,
    LOGOUT = 4,
    SPELL_CAST = 5,
    KILL_PLAYER = 6,
    KILL_CREATURE = 7,
    KILLED_BY_CREATURE = 8,
    DUEL_REQUEST = 9,
    DUEL_START = 10,
    DUEL_END = 11,
    GIVE_XP = 12,
    LEVEL_CHANGE = 13,
    MONEY_CHANGE = 14,
    REPUTATION_CHANGE = 15,
    TALENTS_CHANGE = 16,
    TALENTS_RESET = 17,
    ---@deprecated needs core hook C6
    CHAT = 18,
    ---@deprecated needs core hook C6
    WHISPER = 19,
    ---@deprecated needs core hook C6
    GROUP_CHAT = 20,
    ---@deprecated needs core hook C6
    GUILD_CHAT = 21,
    ---@deprecated needs core hook C6
    CHANNEL_CHAT = 22,
    ---@deprecated needs core hook C5
    EMOTE = 23,
    TEXT_EMOTE = 24,
    SAVE = 25,
    BIND_TO_INSTANCE = 26,
    UPDATE_ZONE = 27,
    MAP_CHANGE = 28,
    ---@deprecated needs core hook C5
    EQUIP = 29,
    FIRST_LOGIN = 30,
    ---@deprecated needs core hook C5
    CAN_USE_ITEM = 31,
    LOOT_ITEM = 32,
    ---@deprecated needs core hook C5
    ENTER_COMBAT = 33,
    ---@deprecated needs core hook C5
    LEAVE_COMBAT = 34,
    REPOP = 35,
    ---@deprecated needs core hook C5
    RESURRECT = 36,
    ---@deprecated needs core hook C5
    LOOT_MONEY = 37,
    ---@deprecated needs core hook C5
    QUEST_ABANDON = 38,
    ---@deprecated needs core hook C5
    LEARN_TALENTS = 39,
    ---@deprecated needs core hook C5
    ENVIRONMENTAL_DEATH = 40,
    ---@deprecated needs core hook C5
    TRADE_ACCEPT = 41,
    ---@deprecated needs core hook C4
    COMMAND = 42,
    ---@deprecated needs core hook C5
    SKILL_CHANGE = 43,
    ---@deprecated needs core hook C5
    LEARN_SPELL = 44,
    ---@deprecated not available (no achievements on Classic 1.60)
    ACHIEVEMENT_COMPLETE = 45,
    ---@deprecated needs core hook C5
    DISCOVER_TAXI = 46,
    UPDATE_AREA = 47,
    ---@deprecated needs core hook C5
    CAN_INIT_TRADE = 48,
    ---@deprecated needs core hook C5
    CAN_SEND_MAIL = 49,
    ---@deprecated not available (no dungeon finder on Classic 1.60)
    CAN_JOIN_LFG = 50,
    ---documented by name; the spec's id table has no id for it
    QUEST_STATUS_CHANGED = 51,
}

---@enum CreatureEvents
CreatureEvents = {
    ---needs core hook C1; the engine names its interim ScriptName path
    ENTER_COMBAT = 1,
    ---needs core hook C1; the engine names its interim ScriptName path
    LEAVE_COMBAT = 2,
    ---fires only for creatures whose ScriptName is forever_lua
    TARGET_DIED = 3,
    ---needs core hook C1; the engine names its interim ScriptName path
    DIED = 4,
    ---needs core hook C1; the engine names its interim ScriptName path
    SPAWN = 5,
    ---fires only for creatures whose ScriptName is forever_lua
    REACH_WP = 6,
    ---fires only for creatures whose ScriptName is forever_lua
    AIUPDATE = 7,
    ---fires only for creatures whose ScriptName is forever_lua
    RECEIVE_EMOTE = 8,
    ---fires only for creatures whose ScriptName is forever_lua
    DAMAGE_TAKEN = 9,
    ---fires only for creatures whose ScriptName is forever_lua
    PRE_COMBAT = 10,
    ---fires only for creatures whose ScriptName is forever_lua
    OWNER_ATTACKED = 12,
    ---fires only for creatures whose ScriptName is forever_lua
    OWNER_ATTACKED_AT = 13,
    ---fires only for creatures whose ScriptName is forever_lua
    HIT_BY_SPELL = 14,
    ---fires only for creatures whose ScriptName is forever_lua
    SPELL_HIT_TARGET = 15,
    ---fires only for creatures whose ScriptName is forever_lua
    JUST_SUMMONED_CREATURE = 19,
    ---fires only for creatures whose ScriptName is forever_lua
    SUMMONED_CREATURE_DESPAWN = 20,
    ---fires only for creatures whose ScriptName is forever_lua
    SUMMONED_CREATURE_DIED = 21,
    ---fires only for creatures whose ScriptName is forever_lua
    SUMMONED = 22,
    ---fires only for creatures whose ScriptName is forever_lua
    RESET = 23,
    ---fires only for creatures whose ScriptName is forever_lua
    REACH_HOME = 24,
    ---fires only for creatures whose ScriptName is forever_lua
    CORPSE_REMOVED = 26,
    ---fires only for creatures whose ScriptName is forever_lua
    MOVE_IN_LOS = 27,
    ---@deprecated needs core hook C9
    DUMMY_EFFECT = 30,
    ---fires only for creatures whose ScriptName is forever_lua
    QUEST_ACCEPT = 31,
    ---fires only for creatures whose ScriptName is forever_lua
    QUEST_REWARD = 34,
    ---fires only for creatures whose ScriptName is forever_lua
    DIALOG_STATUS = 35,
    ---needs core hook C1; the engine names its interim ScriptName path
    ADD = 36,
    ---needs core hook C1; the engine names its interim ScriptName path
    REMOVE = 37,
}

---@enum GameObjectEvents
GameObjectEvents = {
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    AIUPDATE = 1,
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    SPAWN = 2,
    ---@deprecated needs core hook C9
    DUMMY_EFFECT = 3,
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    QUEST_ACCEPT = 4,
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    QUEST_REWARD = 5,
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    DIALOG_STATUS = 6,
    ---@deprecated not available (no destructible game objects on Classic 1.60)
    DESTROYED = 7,
    ---@deprecated not available (no destructible game objects on Classic 1.60)
    DAMAGED = 8,
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    LOOT_STATE_CHANGE = 9,
    ---fires only for game objects whose ScriptName is forever_lua_gameobject
    GO_STATE_CHANGED = 10,
    ---needs core hook C2; the engine names its interim ScriptName path
    ADD = 12,
    ---needs core hook C2; the engine names its interim ScriptName path
    REMOVE = 13,
    ---needs core hook C2; the engine names its interim ScriptName path
    USE = 14,
}

---@enum ItemEvents
ItemEvents = {
    ---@deprecated needs core hook C9
    DUMMY_EFFECT = 1,
    ---needs core hook C3; the engine names its interim ScriptName path
    USE = 2,
    ---fires for items whose ScriptName is forever_lua_item
    QUEST_ACCEPT = 3,
    ---fires for items whose ScriptName is forever_lua_item
    EXPIRE = 4,
    ---fires for items whose ScriptName is forever_lua_item
    REMOVE = 5,
}

---@enum PlayerGossipEvents
PlayerGossipEvents = {
    ---the documentation says HELLO is not applicable to player gossip; the engine registers it
    HELLO = 1,
    ---needs core hook C3; the engine names its interim ScriptName path
    SELECT = 2,
}

---@enum CreatureGossipEvents
CreatureGossipEvents = {
    HELLO = 1,
    SELECT = 2,
}

---@enum GameObjectGossipEvents
GameObjectGossipEvents = {
    HELLO = 1,
    SELECT = 2,
}

---@enum ItemGossipEvents
ItemGossipEvents = {
    ---needs core hook C3; the engine names its interim ScriptName path
    HELLO = 1,
    ---needs core hook C3; the engine names its interim ScriptName path
    SELECT = 2,
}

---@enum GroupEvents
GroupEvents = {
    MEMBER_ADD = 1,
    MEMBER_INVITE = 2,
    MEMBER_REMOVE = 3,
    LEADER_CHANGE = 4,
    DISBAND = 5,
    ---@deprecated needs core hook C10
    CREATE = 6,
}

---@enum GuildEvents
GuildEvents = {
    ADD_MEMBER = 1,
    REMOVE_MEMBER = 2,
    MOTD_CHANGE = 3,
    INFO_CHANGE = 4,
    CREATE = 5,
    DISBAND = 6,
    MONEY_WITHDRAW = 7,
    MONEY_DEPOSIT = 8,
    ---@deprecated not available (no guild bank on Classic 1.60)
    ITEM_MOVE = 9,
    EVENT = 10,
    ---@deprecated not available (no guild bank on Classic 1.60)
    BANK_EVENT = 11,
}

---@enum MapEvents
MapEvents = {
    INITIALIZE = 1,
    ---@deprecated needs core hook C11
    LOAD = 2,
    UPDATE = 3,
    PLAYER_ENTER = 4,
    ---@deprecated needs core hook C11
    CREATURE_CREATE = 5,
    ---@deprecated needs core hook C11
    GAMEOBJECT_CREATE = 6,
    ---@deprecated needs core hook C11
    CHECK_ENCOUNTER = 7,
}

---@enum InstanceEvents
InstanceEvents = {
    INITIALIZE = 1,
    ---@deprecated needs core hook C11
    LOAD = 2,
    UPDATE = 3,
    PLAYER_ENTER = 4,
    ---@deprecated needs core hook C11
    CREATURE_CREATE = 5,
    ---@deprecated needs core hook C11
    GAMEOBJECT_CREATE = 6,
    ---@deprecated needs core hook C11
    CHECK_ENCOUNTER = 7,
}

---@enum PacketEvents
PacketEvents = {
    ---read-only copy of the core packet (ServerScript hook): a returned false cannot change the wire packet
    PACKET_RECEIVE = 5,
    ---read-only copy of the core packet (ServerScript hook): a returned false cannot change the wire packet
    PACKET_SEND = 7,
}

---@enum BattleGroundEvents
BattleGroundEvents = {
    START = 1,
    END = 2,
    CREATE = 3,
    PRE_DESTROY = 4,
}
BGEvents = BattleGroundEvents

---@enum SpellEvents
SpellEvents = {
}

---@enum BotEvents
---our extension
BotEvents = {
    ---our extension
    BOT_ADDED = 1,
    ---our extension
    BOT_REMOVED = 2,
    ---our extension
    ORDER_GIVEN = 3,
    ---our extension
    ORDER_DONE = 4,
    ---our extension
    ORDER_FAILED = 5,
    ---our extension
    BOT_DIED = 6,
    ---our extension
    WIPE = 7,
    ---our extension
    RUN_STEP = 8,
    ---our extension
    RUN_STOPPED = 9,
}
