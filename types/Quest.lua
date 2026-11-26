---@meta

---@class Quest
local Quest = {}

---@return any
function Quest:GetFlags() end

---@return any
function Quest:GetId() end

---@return any
function Quest:GetLevel() end

---@return any
function Quest:GetMaxLevel() end

---@return any
function Quest:GetMinLevel() end

---@return any
function Quest:GetNextQuestId() end

---@return any
function Quest:GetNextQuestInChain() end

---@return any
function Quest:GetPrevQuestId() end

---@return any
function Quest:GetTitle() end

---@return any
function Quest:GetType() end

---@param flag any
---@return any
function Quest:HasFlag(flag) end

---@return any
function Quest:IsDaily() end

---@return any
function Quest:IsRepeatable() end
