-- Greets every player who logs in.
RegisterPlayerEvent(3, function(event, player)
    player:SendBroadcastMessage('Welcome, ' .. player:GetName() .. '!')
end)

-- First login gets a second line (docs: on_first_login is its own event).
RegisterPlayerEvent(30, function(event, player)
    player:SendBroadcastMessage('Type .help for the commands.')
end)
