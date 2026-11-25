-- The asynchronous database form (E3): the callback gets an ElunaQuery (or nil).
async_rows = 0
WorldDBQueryAsync("SELECT entry FROM creature_template ORDER BY entry", function(query)
    if not query then
        async_rows = -1
        return
    end
    repeat
        async_rows = async_rows + query:GetUInt32(0)
    until not query:NextRow()
end)
