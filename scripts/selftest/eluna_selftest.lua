-- Live selftest of Eluna mode (E1): load it as a script on a test server running the module in Eluna mode.
--
-- Expected on a server with ForeverLua.Mode = "eluna":
--   * the lines "Eluna selftest: ..." appear in the log and the ERROR lines are the two expected failures
--     ("Creature no longer exists" for the stored handle, "nil" for the missing table) - nothing else.
--   * a creature with entry 90001 and ScriptName 'forever_lua' answers the "Selftest" gossip option.
--   * no server abort from WorldDBQuery of a missing table.
local ENTRY = 90001
local NEAR = 1000.0
local spawned = nil
local timerFired = false
local missedTable = nil

local function log(line)
    PrintInfo('Eluna selftest: ' .. line)
end

-- 1) Server startup: one timer, a broadcast, and a raw query of a table that does not exist.
RegisterServerEvent(14, function(event)
    log('server startup event ' .. tostring(event))
    CreateLuaEvent(function(id, delay, repeats)
        timerFired = true
        log('timer fired (event ' .. id .. ', delay ' .. delay .. ')')
        SendWorldMessage('Eluna selftest lives')
    end, 1000, 1)

    missedTable = WorldDBQuery('SELECT * FROM forever_lua_no_such_table')
    log('missing table query returned ' .. tostring(missedTable))
end)

-- 1b) The spawn and stored-handle step needs a player in the world, so it runs when "Selftest" logs in (at STARTUP no
-- player is online). DespawnOrUnsummon is deferred by the core, so the stored handle is checked two seconds later.
local function spawnStep(player)
    spawned = player:SpawnCreature(ENTRY, player:GetX() + 2.0, player:GetY(), player:GetZ(), 0.0, 0, 60000)
    log('spawned ' .. tostring(spawned))
    if spawned then
        local stored = spawned
        log('guid roundtrip: ' .. tostring(GetGUIDType(stored:GetGUID())) .. ' entry ' .. tostring(GetGUIDEntry(stored:GetGUID())))
        -- The event is registered BEFORE the despawn: DespawnOrUnsummon(0) removes a temporary summon at once on the
        -- live core, so a method called on the handle afterwards already raises "Creature no longer exists".
        stored:RegisterEvent(function(id, delay, repeats, creature)
            log('object event reached ' .. tostring(creature ~= nil) .. ' (must not print after the despawn)')
        end, 500, 0)
        stored:DespawnOrUnsummon(0)
        CreateLuaEvent(function()
            -- The stored handle must now raise "Creature no longer exists" instead of returning data.
            local ok, err = pcall(function() return stored:GetHealth() end)
            log('stored handle after despawn: ' .. tostring(ok) .. ' / ' .. tostring(err))
        end, 2000, 1)
    end
end

-- 2) Gossip: hello builds the menu, select teleports the player back to the spawn point.
RegisterCreatureGossipEvent(ENTRY, 1, function(event, player, creature)
    player:GossipMenuAddItem(0, 'Selftest: take me back', 1, 1)
    player:GossipSendMenu(100, creature)
    -- returning nothing: this handler built the menu (false would ask for the core's default action)
end)

RegisterCreatureGossipEvent(ENTRY, 2, function(event, player, creature, sender, intid, code, menu_id)
    if sender == 1 and intid == 1 and player then
        player:Teleport(player:GetMapId(), player:GetX(), player:GetY(), player:GetZ(), player:GetO())
    end
    player:GossipComplete()
end)

-- 3) Login welcome and a chat filter (the replacement text is visible; blocking would need C6).
-- 4) A handle kept from the login must answer "no longer exists" once the player is on another map (the access rule asks
-- the object's CURRENT map before anything is looked up), and a failed `.lua reload` must not fire LUA_STATE_CLOSE.
local loginHandle = nil
RegisterPlayerEvent(28, function(event, player)
    if loginHandle then
        local ok, err = pcall(function() return loginHandle:GetHealth() end)
        log('stale player handle after a map change: ' .. tostring(ok) .. ' / ' .. tostring(err))
    end
end)
RegisterServerEvent(16, function(event)
    log('LUA_STATE_CLOSE (only on a reload that is applied)')
end)

RegisterPlayerEvent(3, function(event, player)
    loginHandle = player
    if player:GetName() == 'Selftest' then
        spawnStep(player)
    end
    player:SendBroadcastMessage('Eluna selftest: welcome, ' .. player:GetName() ..
                                   ' (timer=' .. tostring(timerFired) ..
                                   ', missingtable=' .. tostring(missedTable) ..
                                   ', spawn=' .. tostring(spawned ~= nil) .. ')')
end)

RegisterPlayerEvent(18, function(event, player, msg, kind, lang)
    return false, '[selftest] ' .. msg
end)

-- 5) E2: group and guild events (invite a character to a party and leave again; the events log their arguments).
RegisterGroupEvent(1, function(event, group, guid)
    log('group member added ' .. tostring(guid))
end)
RegisterGroupEvent(3, function(event, group, guid, method, kicker, reason)
    log('group member removed ' .. tostring(guid) .. ' (method ' .. tostring(method) .. ')')
end)
RegisterGroupEvent(5, function(event, group)
    log('group disbanded')
end)
RegisterGuildEvent(1, function(event, guild, player, rank)
    log('guild member added ' .. tostring(player and player:GetName()) .. ' rank ' .. tostring(rank))
end)
RegisterGuildEvent(3, function(event, guild, motd)
    log('guild motd changed: ' .. tostring(motd))
end)

-- 6) E2: the Quest class (quest 1 exists in every Classic world database).
RegisterServerEvent(14, function()
    local quest = GetQuest(1)
    if quest then
        log('quest 1: title ' .. tostring(quest:GetTitle()) .. ', level ' .. tostring(quest:GetLevel()) ..
                ', id ' .. tostring(quest:GetId()) .. ', daily ' .. tostring(quest:IsDaily()))
    else
        log('quest 1 is missing from this world database')
    end
end)

-- 7) E3: an asynchronous query, ForeverData and a custom command.
RegisterServerEvent(14, function()
    WorldDBQueryAsync('SELECT 1 AS one', function(query)
        if not query then
            log('async query returned nil')
            return
        end
        repeat
            log('async query row: ' .. tostring(query:GetUInt32(0)))
        until not query:NextRow()
    end)
    local runs = ForeverData.get('selftest_runs') or 0
    ForeverData.set('selftest_runs', runs + 1)
    log('ForeverData selftest_runs: ' .. tostring(ForeverData.get('selftest_runs')))
end)

RegisterCommand('luahello', 0, function(player, args)
    return 'hello ' .. tostring(args[1] or 'world') .. ' (from ForeverLua)'
end, 'greets the given name')
