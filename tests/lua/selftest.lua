-- Toolkit-style `.tt selftest`: starts two timers, answers with one TTJSON line like the agent expects.
Cmd.register('tt', function(ctx)
    if ctx.args[1] ~= 'selftest' then
        ctx:reply('TTJSON ' .. Json.encode({cmd = ctx.args[1], ok = false}))
        return
    end
    Timer.after(100, function() fired_after = fired_after + 1 end)
    Timer.every(50, function() fired_every = fired_every + 1 end, 2)
    ctx:reply('TTJSON ' .. Json.encode({cmd = 'selftest', ok = true, id = ctx.id, timers = 2}))
end, 3)

Event.on('creature.death', function(snap, creature)
    local name = creature:name() or 'gone'
    deaths[#deaths + 1] = snap.entry .. ':' .. name
end)
