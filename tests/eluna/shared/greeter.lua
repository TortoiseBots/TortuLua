-- A module loaded with require('shared.greeter').
local M = {}
function M.greet(player)
    player:SendBroadcastMessage('Hello from the library, ' .. player:GetName())
end
return M
