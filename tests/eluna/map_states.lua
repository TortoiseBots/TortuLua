-- E4: the state a script runs in. In a map state GetStateMap/GetStateMapId name the map, GetPlayersOnMap answers the
-- players there; in the world state GetStateMapId is -1 and IsCompatibilityMode tells the server's mode.
local state = GetStateMapId()
compat_mode = IsCompatibilityMode()

if state == -1 then
    world_state_seen = true
else
    map_state_seen = state
    RegisterMapEvent(state, 3, function(event, instance_data, map, diff)
        map_ticks = (map_ticks or 0) + 1
        map_diff = diff
        local players = GetPlayersOnMap(2)
        map_players = #players
        if map then
            state_map_id = map:GetMapId()
        end
    end)
end
