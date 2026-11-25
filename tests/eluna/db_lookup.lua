-- Reads creature names through the world database (Eluna's raw SQL), tolerating a missing table.
rows = 0
lastname = ''
local q = WorldDBQuery('SELECT entry, name FROM creature_template ORDER BY entry')
if q then
    repeat
        rows = rows + 1
        lastname = q:GetString(1)
    until not q:NextRow()
end
missing = WorldDBQuery('SELECT * FROM no_such_table')
