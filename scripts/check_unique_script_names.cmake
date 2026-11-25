# The core keeps ONE name table across every script type of a context (ScriptRegistryCompositum::
# SetScriptNameInContext asserts "Scriptname was assigned to this context already!" and the failed assert is a SIGSEGV at
# ScriptMgr::Initialize). So a CreatureScript, a GameObjectScript and an ItemScript may NOT share a name (the first
# live boot crashed on "forever_lua" given to all three). Every `XxxScript("name")` constructor in src/ must carry a name
# no other one carries.
# Usage: cmake -DSRC=<src folder> -P check_unique_script_names.cmake
cmake_policy(SET CMP0057 NEW)
file(GLOB_RECURSE files ${SRC}/*.cpp ${SRC}/*.h)
set(names "")
set(dups "")
set(count 0)
foreach(f ${files})
  file(READ ${f} text)
  string(REGEX MATCHALL "[A-Za-z]+Script\\(\"[A-Za-z0-9_]+\"\\)" hits "${text}")
  foreach(h ${hits})
    string(REGEX REPLACE "^[A-Za-z]+Script\\(\"([A-Za-z0-9_]+)\"\\)$" "\\1" n "${h}")
    math(EXPR count "${count} + 1")
    if("${n}" IN_LIST names)
      list(APPEND dups "${n} (again in ${f})")
    endif()
    list(APPEND names "${n}")
  endforeach()
endforeach()
if(count EQUAL 0)
  message(FATAL_ERROR "no script constructors found under ${SRC}: the scan itself is broken")
endif()
if(dups)
  message(FATAL_ERROR "script names shared by two script classes (the core asserts on this):\n${dups}")
endif()
message(STATUS "${count} script names, all unique, in ${SRC}")
