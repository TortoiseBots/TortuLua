-- ForeverData (E3): the value survives the reload of this file.
local visits = ForeverData.get("visits") or 0
ForeverData.set("visits", visits + 1)
function visits_now() return ForeverData.get("visits") end
