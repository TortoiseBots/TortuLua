-- Group and guild events (E2): the argument lists are the documentation ones.
RegisterGroupEvent(1, function(event, group, guid)
    group_added = guid
end)
RegisterGroupEvent(3, function(event, group, guid, method, kicker, reason)
    group_removed = { guid, method, tostring(reason) }
end)
RegisterGroupEvent(5, function(event, group)
    group_disbanded = true
end)
RegisterGuildEvent(1, function(event, guild, player, rank)
    guild_joined = { player:GetGUIDLow(), rank }
end)
RegisterGuildEvent(3, function(event, guild, motd)
    guild_motd = motd
end)
