-- Using the item (entry 90003) teleports the player; USE may return false to cancel the spell.
RegisterItemEvent(90003, 2, function(event, player, item, target)
    player:Teleport(0, -8913.2, 554.6, 93.8, 0)
end)
