-- A kill pays 10 silver; experience is doubled by the GIVE_XP handler.
RegisterPlayerEvent(7, function(event, killer, killed)
    killer:ModifyMoney(1000)
end)

RegisterPlayerEvent(12, function(event, player, amount, victim)
    return amount * 2
end)
