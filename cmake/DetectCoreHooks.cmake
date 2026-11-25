# ForeverLua: detect the core hooks of the maintainers' API specification, section 6 (C1-C4) in the ScriptMgr.h of the core the
# module is built against, and turn them into FL_CORE_HOOK_* macros (cmake/core_hooks.h.in). The module compiles the
# hook adapters only when the declaration is really there, and the engine is told the same set at runtime, so it logs a
# "needs core hook" line exactly for the hooks the core lacks.
#
# C1/C2 are notify-only hooks (the module never replaces a creature or game object AI): AllCreatureScript with
# OnCreatureAddWorld/OnCreatureRemoveWorld, plus the combat notifications OnCreatureEngage/OnCreatureLeaveCombat/
# OnCreatureJustDied/OnCreatureAppear; AllGameObjectScript with OnGameObjectAddWorld/OnGameObjectRemoveWorld, plus
# OnGameObjectUse.
#
# A method counts only inside the body of its own class (AllCreatureScript, AllGameObjectScript, PlayerScript): the same
# name declared in an unrelated script class never enables a hook (it would break the `override` build or lie in the
# runtime mask).
#
# The scan matches class and method names only, never a parameter name, a spacing or a return type: a reworded
# declaration during upstream review must not switch a hook off silently, and the adapters are `override`s that fail the
# build on a real signature mismatch. Every hook is a `virtual` method of its script class (the dispatcher in ScriptMgr
# has a non-virtual method of the same name, for example OnItemUse, which must not count). Comments are stripped first so a hook named only in a comment is not a hook.
# `try_compile` is not usable here: ScriptMgr.h pulls the whole game/database/boost include graph, and a configure-time
# try_compile of that graph fails for reasons unrelated to the hooks (the module CMakeLists runs before the core targets
# exist in a standalone build). The caller makes CMake reconfigure when the header changes (CMakeLists.txt).
# The text of the body of `class TC_GAME_API <NAME>` (between its braces, nested braces balanced), or empty when the class is
# not declared. A method counts for a hook only inside the class it belongs to: the same name in an unrelated script class
# (or the non-virtual ScriptMgr dispatcher) is not that hook.
function(_forever_lua_class_body TEXT NAME OUTVAR)
    set(${OUTVAR} "" PARENT_SCOPE)
    if(NOT "${TEXT}" MATCHES "class[ \t\r\n]+TC_GAME_API[ \t\r\n]+${NAME}([ \t\r\n:{;]|$)")
        return()
    endif()
    string(REGEX MATCH "class[ \t\r\n]+TC_GAME_API[ \t\r\n]+${NAME}([ \t\r\n:{;]|$)" _head "${TEXT}")
    string(FIND "${TEXT}" "${_head}" _at)
    string(SUBSTRING "${TEXT}" ${_at} -1 _rest)
    string(FIND "${_rest}" "{" _open)
    string(FIND "${_rest}" ";" _semi)
    if(_open LESS 0 OR (_semi GREATER_EQUAL 0 AND _semi LESS _open))
        return()   # a forward declaration, not a definition
    endif()
    math(EXPR _from "${_open} + 1")
    string(SUBSTRING "${_rest}" ${_from} -1 _rest)
    set(_depth 1)
    set(_body "")
    while(_depth GREATER 0)
        string(FIND "${_rest}" "{" _o)
        string(FIND "${_rest}" "}" _c)
        if(_c LESS 0)
            return()   # unbalanced: not a class body we can trust
        endif()
        if(_o GREATER_EQUAL 0 AND _o LESS _c)
            math(EXPR _n "${_o} + 1")
            string(SUBSTRING "${_rest}" 0 ${_n} _piece)
            math(EXPR _depth "${_depth} + 1")
        else()
            math(EXPR _n "${_c} + 1")
            string(SUBSTRING "${_rest}" 0 ${_n} _piece)
            math(EXPR _depth "${_depth} - 1")
        endif()
        string(APPEND _body "${_piece}")
        string(SUBSTRING "${_rest}" ${_n} -1 _rest)
    endwhile()
    set(${OUTVAR} "${_body}" PARENT_SCOPE)
endfunction()

# 1 when the class body declares `virtual ... NAME(`.
function(_forever_lua_has_method BODY NAME OUTVAR)
    if("${BODY}" MATCHES "virtual[^;(]*[ \t\r\n*&]${NAME}[ \t\r\n]*\\(")
        set(${OUTVAR} 1 PARENT_SCOPE)
    else()
        set(${OUTVAR} 0 PARENT_SCOPE)
    endif()
endfunction()

function(forever_lua_detect_core_hooks HEADER)
    set(_c1 0)
    set(_c1_events 0)
    set(_c2 0)
    set(_c2_use 0)
    set(_gossip 0)
    set(_itemuse 0)
    set(_command 0)
    if(EXISTS "${HEADER}")
        file(READ "${HEADER}" _text)
        string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" "" _text "${_text}")
        string(REGEX REPLACE "//[^\n]*" "" _text "${_text}")
        _forever_lua_class_body("${_text}" "AllCreatureScript" _creatureBody)
        _forever_lua_class_body("${_text}" "AllGameObjectScript" _gameObjectBody)
        _forever_lua_class_body("${_text}" "PlayerScript" _playerBody)
        # C1: AllCreatureScript (add/remove, and the notify-only creature events), inside that class only.
        if(NOT "${_creatureBody}" STREQUAL "")
            _forever_lua_has_method("${_creatureBody}" "OnCreatureAddWorld" _a)
            _forever_lua_has_method("${_creatureBody}" "OnCreatureRemoveWorld" _b)
            if(_a AND _b)
                set(_c1 1)
                _forever_lua_has_method("${_creatureBody}" "OnCreatureEngage" _e1)
                _forever_lua_has_method("${_creatureBody}" "OnCreatureLeaveCombat" _e2)
                _forever_lua_has_method("${_creatureBody}" "OnCreatureJustDied" _e3)
                _forever_lua_has_method("${_creatureBody}" "OnCreatureAppear" _e4)
                if(_e1 AND _e2 AND _e3 AND _e4)
                    set(_c1_events 1)
                endif()
            endif()
        endif()
        # C2: AllGameObjectScript (add/remove, and the notify-only use), inside that class only.
        if(NOT "${_gameObjectBody}" STREQUAL "")
            _forever_lua_has_method("${_gameObjectBody}" "OnGameObjectAddWorld" _a)
            _forever_lua_has_method("${_gameObjectBody}" "OnGameObjectRemoveWorld" _b)
            if(_a AND _b)
                set(_c2 1)
                _forever_lua_has_method("${_gameObjectBody}" "OnGameObjectUse" _c2_use)
            endif()
        endif()
        # C3/C4: PlayerScript only (OnGossipSelect is also a virtual of the creature/game object/item script classes; the
        # player one is the one with an ObjectGuid source parameter).
        if(NOT "${_playerBody}" STREQUAL "")
            if("${_playerBody}" MATCHES "virtual[^;(]*[ \t\r\n*&]OnGossipSelect[ \t\r\n]*\\([^)]*ObjectGuid")
                set(_gossip 1)
            endif()
            _forever_lua_has_method("${_playerBody}" "OnItemUse" _itemuse)
            _forever_lua_has_method("${_playerBody}" "OnCommand" _command)
        endif()
    endif()

    # Bit values must match fl::CoreHook in src/engine/Engine.h.
    set(_mask 0)
    if(_c1)
        math(EXPR _mask "${_mask} + 1")
    endif()
    if(_c2)
        math(EXPR _mask "${_mask} + 2")
    endif()
    if(_gossip)
        math(EXPR _mask "${_mask} + 4")
    endif()
    if(_itemuse)
        math(EXPR _mask "${_mask} + 8")
    endif()
    if(_command)
        math(EXPR _mask "${_mask} + 16")
    endif()
    if(_c1_events)
        math(EXPR _mask "${_mask} + 32")
    endif()
    if(_c2_use)
        math(EXPR _mask "${_mask} + 64")
    endif()

    set(FL_CORE_HOOK_C1 "${_c1}" PARENT_SCOPE)
    set(FL_CORE_HOOK_C1_EVENTS "${_c1_events}" PARENT_SCOPE)
    set(FL_CORE_HOOK_C2 "${_c2}" PARENT_SCOPE)
    set(FL_CORE_HOOK_C2_USE "${_c2_use}" PARENT_SCOPE)
    set(FL_CORE_HOOK_C3_GOSSIP "${_gossip}" PARENT_SCOPE)
    set(FL_CORE_HOOK_C3_ITEM_USE "${_itemuse}" PARENT_SCOPE)
    set(FL_CORE_HOOK_C4 "${_command}" PARENT_SCOPE)
    set(FL_CORE_HOOK_MASK "${_mask}" PARENT_SCOPE)
endfunction()
