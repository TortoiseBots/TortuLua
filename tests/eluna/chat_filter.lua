-- Replaces what players say (return false, newMessage). Blocking them needs core hook C6.
RegisterPlayerEvent(18, function(event, player, msg, kind, lang)
    return false, '[filtered] ' .. msg
end)
