-- Quest and Item classes (E2).
local quest = GetQuest(100)
if quest then
    quest_title = quest:GetTitle()
    quest_level = quest:GetLevel()
    quest_daily = quest:IsDaily()
end
