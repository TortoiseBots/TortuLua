-- On entering combat the boss schedules one spell; on death it clears its timers.
local ENTRY = 90002

RegisterCreatureEvent(ENTRY, 1, function(event, creature, target)
    creature:RegisterEvent(function()
        creature:CastSpell(target, 1234, true)
    end, 500, 1)
end)

RegisterCreatureEvent(ENTRY, 4, function(event, creature, killer)
    creature:RemoveEvents()
end)
