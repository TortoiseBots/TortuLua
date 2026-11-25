-- Uses a shared module through require (dots map to folders).
local greeter = require('shared.greeter')
RegisterPlayerEvent(3, function(event, player)
    greeter.greet(player)
end)
