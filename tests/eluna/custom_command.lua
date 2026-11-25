-- RegisterCommand (E3, our extension): security 0, a string return goes back to the caller.
RegisterCommand("luahello", 0, function(player, args)
    local who = args[1] or "world"
    return "hello " .. who
end, "greets the given name")
