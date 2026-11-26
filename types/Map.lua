---@meta

---@class Map
local Map = {}

---@return any
function Map:Data() end

---@param arg1 any
---@param arg2 any
---@param arg3 any
---@return any
function Map:GetAreaId(arg1, arg2, arg3) end

---@return any
function Map:GetDifficulty() end

---@param arg1 any
---@param arg2 any
---@return any
function Map:GetHeight(arg1, arg2) end

---@deprecated not available (this core); no generic instance data store to read (see the Map class note)
---@return any
function Map:GetInstanceData() end

---@return any
function Map:GetInstanceId() end

---@return any
function Map:GetMapId() end

---@return any
function Map:GetName() end

---@return any
function Map:GetPlayerCount() end

---@return any
function Map:GetPlayers() end

---@param arg1 any
---@return any
function Map:GetWorldObject(arg1) end

---@return any
function Map:IsArena() end

---@return any
function Map:IsBattleground() end

---@return any
function Map:IsDungeon() end

---@return any
function Map:IsEmpty() end

---@return any
function Map:IsHeroic() end

---@return any
function Map:IsRaid() end

---@deprecated not available (this core); no generic instance data store to save (see the Map class note)
---@return any
function Map:SaveInstanceData() end

---@param zone any
---@param type any
---@param arg3 any
---@return any
function Map:SetWeather(zone, type, arg3) end
