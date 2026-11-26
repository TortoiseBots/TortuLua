---@meta

---@class WorldObject : Object
local WorldObject = {}

---@return any
function WorldObject:Data() end

---@param x any
---@param y any
---@return any
function WorldObject:GetAngle(x, y) end

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

---@param x any
---@param y any
---@return any
function WorldObject:GetDistance2d(x, y) end

---@param x any
---@param y any
---@param z any
---@return any
function WorldObject:GetExactDistance(x, y, z) end

---@param x any
---@param y any
---@return any
function WorldObject:GetExactDistance2d(x, y) end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function WorldObject:GetGameObjectsInRange(arg1, arg2, arg3) end

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
function WorldObject:GetNearObject() end

---@return any
function WorldObject:GetNearObjects() end

---@return any
function WorldObject:GetO() end

---@deprecated not available (this core)
---@return any
function WorldObject:GetPhaseMask() end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function WorldObject:GetPlayersInRange(arg1, arg2, arg3) end

---@param distance any
---@param angle any
---@return any
function WorldObject:GetRelativePoint(distance, angle) end

---@return any
function WorldObject:GetX() end

---@return any
function WorldObject:GetY() end

---@return any
function WorldObject:GetZ() end

---@return any
function WorldObject:GetZoneId() end

---@param target any
---@param arc any
---@return any
function WorldObject:IsInBack(target, arc) end

---@param target any
---@param arc any
---@return any
function WorldObject:IsInFront(target, arc) end

---@param target any
---@return any
function WorldObject:IsInMap(target) end

---@param target any
---@param minrange any
---@param maxrange any
---@param arg4 any
---@return any
function WorldObject:IsInRange(target, minrange, maxrange, arg4) end

---@param x any
---@param y any
---@param minrange any
---@param maxrange any
---@return any
function WorldObject:IsInRange2d(x, y, minrange, maxrange) end

---@param x any
---@param y any
---@param z any
---@param minrange any
---@param maxrange any
---@return any
function WorldObject:IsInRange3d(x, y, z, minrange, maxrange) end

---@param target any
---@param distance any
---@param arg3 any
---@return any
function WorldObject:IsWithinDist(target, distance, arg3) end

---@param x any
---@param y any
---@param distance any
---@return any
function WorldObject:IsWithinDist2d(x, y, distance) end

---@param x any
---@param y any
---@param z any
---@param distance any
---@return any
function WorldObject:IsWithinDist3d(x, y, z, distance) end

---@param target any
---@param distance any
---@param arg3 any
---@return any
function WorldObject:IsWithinDistInMap(target, distance, arg3) end

---@param x any
---@param y any
---@param z any
---@return any
function WorldObject:IsWithinLoS(x, y, z) end

---@param arg1 any
---@param player any
---@return any
function WorldObject:PlayDirectSound(arg1, player) end

---@param arg1 any
---@param player any
---@return any
function WorldObject:PlayDistanceSound(arg1, player) end

---@param arg1 any
---@param player any
---@return any
function WorldObject:PlayMusic(arg1, player) end

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

---@return any
function WorldObject:SendPacket() end

---@param arg1 any
---@param language any
---@return any
function WorldObject:SendUnitSay(arg1, language) end

---@param arg1 any
---@param lang any
---@param receiver any
---@param arg4 any
---@return any
function WorldObject:SendUnitWhisper(arg1, lang, receiver, arg4) end

---@param arg1 any
---@param language any
---@return any
function WorldObject:SendUnitYell(arg1, language) end

---@deprecated not available (this core)
---@return any
function WorldObject:SetPhaseMask() end

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
