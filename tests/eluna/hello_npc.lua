-- A creature that offers one gossip option; selecting it teleports the player. Eluna-style, our own example.
local ENTRY = 90001

RegisterCreatureGossipEvent(ENTRY, 1, function(event, player, creature)
    player:GossipMenuAddItem(0, 'Take me to the inn', 1, 1)
    player:GossipSendMenu(100, creature)
    -- returning nothing: this handler built the menu (false would ask for the core's default action)
end)

RegisterCreatureGossipEvent(ENTRY, 2, function(event, player, creature, sender, intid, code, menu_id)
    if sender == 1 and intid == 1 then
        player:Teleport(1, -8913.2, 554.6, 93.8, 0)
    end
    player:GossipComplete()
end)
