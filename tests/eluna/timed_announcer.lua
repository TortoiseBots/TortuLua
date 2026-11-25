-- Announces three times, then unregisters itself.
announced = 0
local id
id = CreateLuaEvent(function(eventId, delay, repeats)
    announced = announced + 1
    SendWorldMessage('Server restart in 5 minutes (' .. announced .. ')')
    if announced >= 3 then
        RemoveEventById(eventId)
    end
end, 60000, 0)
