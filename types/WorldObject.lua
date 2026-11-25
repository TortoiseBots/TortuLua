---@meta

---@class WorldObject : Object
local WorldObject = {}

---@return any
function WorldObject:GetAreaId() end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@return any
function WorldObject:GetCreaturesInRange(arg1, arg2, arg3, arg4) end

---@param x any
---@param y any
---@param z any
---@return any
function WorldObject:GetDistance(x, y, z) end

---@return any
function WorldObject:GetInstanceId() end

---@return any
function WorldObject:GetLocation() end

---@return any
function WorldObject:GetMap() end

---@return any
function WorldObject:GetMapId() end

---@return any
function WorldObject:GetName() end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@param arg4 any
---@return any
function WorldObject:GetNearestCreature(arg1, arg2, arg3, arg4) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function WorldObject:GetNearestGameObject(arg1, arg2, arg3) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function WorldObject:GetNearestPlayer(arg1, arg2, arg3) end

---@return any
function WorldObject:GetO() end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function WorldObject:GetPlayersInRange(arg1, arg2, arg3) end

---@return any
function WorldObject:GetX() end

---@return any
function WorldObject:GetY() end

---@return any
function WorldObject:GetZ() end

---@return any
function WorldObject:GetZoneId() end

---@param target any
---@param distance any
---@param arg3 any
---@return any
function WorldObject:IsWithinDistInMap(target, distance, arg3) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function WorldObject:RegisterEvent(arg1, arg2, arg3) end

---@param eventId any
---@return any
function WorldObject:RemoveEventById(eventId) end

---@return any
function WorldObject:RemoveEvents() end

---@param entry any
---@param x any
---@param y any
---@param z any
---@param o any
---@param arg6 any
---@param arg7 any
---@return any
function WorldObject:SpawnCreature(entry, x, y, z, o, arg6, arg7) end

---@param entry any
---@param x any
---@param y any
---@param z any
---@param o any
---@param arg6 any
---@return any
function WorldObject:SummonGameObject(entry, x, y, z, o, arg6) end
