-- Live selftest of the bot orders (the maintainers' bot API specification section 6, stage B2): when a party bot's target drops
-- below 30 % health, every bot of that party is ordered to flee once, and each order's outcome is logged.
--
-- Expected on a server with ForeverLua.Mode = "eluna" and the bot build:
--   * the lines "Bot spread selftest: ..." (PrintInfo) show one "ordered <bot> ticket <n>" per bot of the party the
--     first time a creature drops below 30 % while a bot of that party is attacking it;
--   * every ticket ends within 5 s with "ticket <n> <bot> flee done seq <s>", or with "failed <code> <msg>" when the
--     bot engine refused it;
--   * one ticket per bot, and `.lua errors` is empty.
local TICK_MS = 500
local THRESHOLD = 30
local spread = {}   -- victim guid -> true: that drop already ordered the spread

local function log(line)
    PrintInfo('Bot spread selftest: ' .. line)
end

-- ORDER_DONE and ORDER_FAILED of the spec's bot events: the outcome of every ticket the timer below hands over.
RegisterBotEvent(4, function(event, bot, ticket, seq, verb)
    log('ticket ' .. ticket .. ' ' .. tostring(bot and bot:GetName()) .. ' ' .. verb .. ' done seq ' .. seq)
end)

RegisterBotEvent(5, function(event, bot, ticket, seq, verb, code, msg)
    log('ticket ' .. ticket .. ' ' .. tostring(bot and bot:GetName()) .. ' ' .. verb ..
        ' failed ' .. code .. ' ' .. tostring(msg))
end)

-- The bots of the bot's party (a bot in no party is its own group of one).
local function partyOf(bot)
    local group = bot:GetGroup()
    if group then
        return GetBotsInGroup(group)
    end
    return { bot }
end

-- Every 500 ms: whose target is below 30 % health? The first sighting of one creature orders the whole party once.
CreateLuaEvent(function()
    for _, bot in ipairs(GetBots()) do
        local player = bot:GetPlayer()
        local victim = player and player:GetVictim()
        if victim and victim:GetHealthPct() < THRESHOLD then
            local key = victim:GetGUID()
            if not spread[key] then
                spread[key] = true
                local party = partyOf(bot)
                log('victim ' .. key .. ' below ' .. THRESHOLD .. '%: ' .. #party .. ' bot(s) spread')
                for _, member in ipairs(party) do
                    local ticket, code, msg = member:Flee()
                    if ticket then
                        log('ordered ' .. member:GetName() .. ' ticket ' .. ticket)
                    else
                        log('refused ' .. member:GetName() .. ' ' .. tostring(code) .. ' ' .. tostring(msg))
                    end
                end
            end
        end
    end
end, TICK_MS, 0)
