---@meta

---@class BattleGround
local BattleGround = {}

---@param team any
---@return any
function BattleGround:GetAlivePlayersCountByTeam(team) end

---@param kills any
---@return any
function BattleGround:GetBonusHonorFromKillCount(kills) end

---@return any
function BattleGround:GetBracketId() end

---@return any
function BattleGround:GetEndTime() end

---@param team any
---@return any
function BattleGround:GetFreeSlotsForTeam(team) end

---@return any
function BattleGround:GetInstanceId() end

---@return any
function BattleGround:GetMap() end

---@return any
function BattleGround:GetMapId() end

---@return any
function BattleGround:GetMaxLevel() end

---@return any
function BattleGround:GetMaxPlayers() end

---@return any
function BattleGround:GetMaxPlayersPerTeam() end

---@return any
function BattleGround:GetMinLevel() end

---@return any
function BattleGround:GetMinPlayers() end

---@return any
function BattleGround:GetMinPlayersPerTeam() end

---@return any
function BattleGround:GetName() end

---@return any
function BattleGround:GetStatus() end

---@return any
function BattleGround:GetTypeId() end

---@return any
function BattleGround:GetWinner() end
