-- TortuLua live self-test (TEST SERVERS ONLY). Registers `.tt selftest [entry=<creature entry>]` and `.tt selftest-loop`.
-- Stand on the ground in the open, then run `.tt selftest #sx1`. It spawns a creature next to you, reads it, kills it,
-- waits for the creature.death event and for the handle to read as gone after the despawn, fires a timer, reads
-- creature_template, asks for a missing table (refused, the server stays up), and answers with ONE system line
-- `TTJSON {"cmd":"selftest","ok":true,"checks":{...}}` (read 2 / final true; read 1 comes one tick in, because
-- Db.select is asynchronous: its answers arrive in callbacks).
-- `.tt selftest-loop` runs an endless loop: the command budget must cut it off (the reply says so) and the next command
-- must still work. Run both, then `.lua reload`, and run both again.
--
-- It uses the toolkit's dispatcher when tt_core.lua is loaded, and brings its own otherwise.

local ok, TT = pcall(require, "tt_core")
if not ok or type(TT) ~= "table" then
    TT = rawget(_G, "TT")
end
if not TT then
    TT = { subs = {}, VERSION = 1 }
    Cmd.register("tt", function(ctx)
        local sub = ctx.args[1] or "help"
        local h = TT.subs[sub]
        if not h then
            ctx:reply("TTJSON " .. Json.encode({ v = 1, cmd = sub, id = ctx.id, ok = false, err = "usage" }))
            return
        end
        h(ctx, ctx.player, { pos = { sub }, kv = {}, flags = {}, id = ctx.id, cmd = sub })
    end, 3)
    TT.__loaded = true
    _G.TT = TT
end

local function reply(sink, t)
    t.v = 1
    t.ts = World.now()
    local line = "TTJSON " .. Json.encode(t)
    if sink.reply then sink:reply(line) else sink:message(line) end
end

TT.subs["selftest-loop"] = function(ctx)
    -- never returns: the command budget (ForeverLua.CommandBudgetMs) ends it with the error "budget"
    local n = 0
    while true do n = n + 1 end
end

TT.subs["selftest"] = function(ctx, player, req)
    req = req or { kv = {}, id = ctx.id }
    if not player then
        return reply(ctx, { cmd = "selftest", id = ctx.id, ok = false, err = "needs_player" })
    end
    local checks, fails = {}, {}
    local function check(name, cond, detail)
        checks[name] = cond and true or false
        if not cond then fails[#fails + 1] = name .. (detail and (": " .. tostring(detail)) or "") end
        return cond
    end

    local map = player:map()
    local x, y, z, o = player:pos()
    check("map", map and map:valid() and true or false)
    if not map then
        return reply(ctx, { cmd = "selftest", id = ctx.id, ok = false, err = "no_map" })
    end

    -- The rest runs from callbacks: Db.select is asynchronous (round 3), so every answer arrives on a later tick.
    local function continueSelftest(entry)
        local deaths = {}
        local handlerId = Event.on("creature.death", function(snap, creature, killer)
            deaths[#deaths + 1] = { snap = snap, creature = creature }
        end, { entry = entry })

        local c, spawnErr = map:spawnCreature(entry or 0, x + 3, y, z, o, { despawnMs = 60000 })
        check("spawn", c ~= nil, spawnErr)
        if not c then
            Event.off(handlerId)
            return reply(player, { cmd = "selftest", id = req.id, ok = false, err = "spawn_failed", msg = tostring(spawnErr), checks = checks })
        end
        check("handle_valid", c:valid() == true)
        check("read_entry", c:entry() == entry)
        check("read_alive", c:alive() == true)
        local cx, cy, cz = c:pos()
        check("read_pos", cx ~= nil and math.abs(cx - (x + 3)) < 1.5)
        check("distance", (c:distanceTo(player) or 99) < 6)
        check("guid_string", type(c:guid()) == "string" and #c:guid() > 5)
        check("map_of_creature", c:map() == map or (c:map() and c:map():id() == map:id()))
        check("map_find", map:find(c:guid()) == c)
        local guid = c:guid()
        local timerFired = false
        Timer.after(100, function() timerFired = true end)

        reply(player, { cmd = "selftest", id = req.id, ok = true, read = 1, final = false, spawned = guid, entry = entry })

        local playerGuid = player:guid()
        local function finish(stage)
            local p = World.player(playerGuid)
            Event.off(handlerId)
            local t = { cmd = "selftest", id = req.id, ok = (#fails == 0), read = 2, final = true, checks = checks, fails = fails, stage = stage }
            if p then reply(p, t) end
        end

        local kill_ok = c:kill()
        check("kill_call", kill_ok == true)
        Timer.after(400, function()
            check("death_event", #deaths >= 1 and deaths[1].snap.entry == entry and deaths[1].snap.guid == guid, #deaths)
            check("death_snapshot_pos", #deaths >= 1 and type(deaths[1].snap.x) == "number")
            check("dead_reads", c:alive() == false or c:valid() == false)
            check("timer_fired", timerFired)
            c:despawn(0)
            Timer.after(800, function()
                check("gone_after_despawn", c:valid() == false)
                local alive, why2 = c:kill()
                check("action_on_gone", alive == false and why2 == "gone", why2)
                check("read_on_gone", c:name() == nil)
                finish("done")
            end)
        end)

    end

    local entry = tonumber(req.kv and req.kv.entry or nil)

    -- An early DB refusal is synchronous: Db.select returns nil, reason and never calls the callback. Report it as a
    -- final answer, so a schema or argument problem cannot leave the command silent.
    local function failFinal(stage, why)
        local p = World.player(player:guid())
        if p then
            reply(p, { cmd = "selftest", id = req.id, ok = false, read = 2, final = true, stage = stage,
                msg = tostring(why), checks = checks })
        end
    end

    local function afterDb(entryFromDb)
        if not entryFromDb then
            check("template_read", false)
            return failFinal("template_read", "no creature_template row")
        end
        check("template_read", true)
        local queued2, err2 = Db.select("world", "creature_template", { "entry" }, { entry = entryFromDb },
            function(rows2, why2)
                if not rows2 or #rows2 ~= 1 then
                    check("db_select", false, why2)
                    return failFinal("db_select", why2)
                end
                check("db_select", true)
                -- a refused query answers in the return values, not in the callback
                local missing, why = Db.select("world", "zz_no_such_table", { "entry" }, function() end)
                check("db_missing_table_refused", missing == nil and why == "no_table", why)
                check("db_has", Db.has("world", "creature_template", "entry") == true and Db.has("world", "zz_no_such_table") == false)
                check("json", Json.decode(Json.encode({ a = 1, b = { 1, 2 } })).b[2] == 2)
                continueSelftest(entryFromDb)
            end)
        if not queued2 then
            check("db_select", false, err2)
            failFinal("db_select", err2)
        end
    end

    if entry then
        afterDb(entry)
    else
        -- the first low-level template of the world database (no where: the 1.14 schema has no minlevel/maxlevel)
        local queued, err = Db.select("world", "creature_template", { "entry" }, nil, { limit = 1, order = "entry" },
            function(rows, why)
                if not rows or not rows[1] then
                    check("template_read", false, why)
                    return failFinal("template_read", why)
                end
                afterDb(rows[1].entry)
            end)
        if not queued then
            check("template_read", false, err)
            failFinal("template_read", err)
        end
    end
end

return TT
