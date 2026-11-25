# TortuLua: a small Lua 5.4 host for ForeverCore test servers (GPL-2.0-or-later, see COPYING and NOTICE). The CMake
# project, the module folder and the option keep the ForeverLua name (see below).
#
# Two ways to use this file:
#  1. Inside a ForeverCore tree (link or clone this repository at <core>/src/modules/ForeverLua): the core adds the
#     folder by itself, `trinity_add_module` exists, and the module is built only with -DBUILD_FOREVER_LUA=1 (default OFF).
#  2. Standalone (cmake -S . -B build): no core is needed. It builds the vendored Lua, the game-free engine and the unit
#     tests (option FOREVER_LUA_BUILD_TESTS, default ON).
# The engine (src/engine) never includes a core header; only src/host (round 2) talks to the core.

if(COMMAND trinity_add_module)
  option(BUILD_FOREVER_LUA "Build the ForeverLua module" 0)
  if(NOT BUILD_FOREVER_LUA)
    return()
  endif()
  set(FOREVER_LUA_IN_CORE 1)
else()
  cmake_minimum_required(VERSION 3.16)
  project(ForeverLua LANGUAGES C CXX)
  set(FOREVER_LUA_IN_CORE 0)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
  endif()
endif()

# Which of the core hooks C1-C4 of the maintainers' API specification, section 6 this core tree has (PRs Karrz1/TrinityCore
# #375-#379; C1/C2 are notify-only, the module never replaces an AI). The adapters in src/host compile only for a hook
# that is really in ScriptMgr.h; the engine gets the same set at runtime and logs "needs core hook Cn" only for the missing ones.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DetectCoreHooks.cmake)
if(FOREVER_LUA_IN_CORE)
  forever_lua_detect_core_hooks("${CMAKE_SOURCE_DIR}/src/server/game/Scripting/ScriptMgr.h")
  # The mask is computed here, at configure time: a change of the core header (a core PR merged, reverted or reworded)
  # must run the configure step again, or a stale mask misses a hook or claims one that is gone.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/server/game/Scripting/ScriptMgr.h")
else()
  forever_lua_detect_core_hooks("")
endif()
message(STATUS "ForeverLua: core hooks C1=${FL_CORE_HOOK_C1} C1-events=${FL_CORE_HOOK_C1_EVENTS} C2=${FL_CORE_HOOK_C2} C2-use=${FL_CORE_HOOK_C2_USE} C3-gossip=${FL_CORE_HOOK_C3_GOSSIP} C3-item-use=${FL_CORE_HOOK_C3_ITEM_USE} C4=${FL_CORE_HOOK_C4}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/core_hooks.h.in ${CMAKE_CURRENT_BINARY_DIR}/fl_core_hooks.h @ONLY)

option(FOREVER_LUA_SANITIZE "Build with AddressSanitizer and UBSan (standalone unit tests)" OFF)
if(FOREVER_LUA_SANITIZE AND NOT FOREVER_LUA_IN_CORE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

option(FOREVER_LUA_TSAN "Build with ThreadSanitizer (standalone unit tests; run under setarch -R where ASLR breaks TSan)" OFF)
if(FOREVER_LUA_TSAN AND NOT FOREVER_LUA_IN_CORE)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

add_subdirectory(dep/lua)
add_subdirectory(src/engine)

if(FOREVER_LUA_IN_CORE)
  add_library(forever-lua STATIC
    src/host/AccessRules.cpp
    src/host/CommandGate.cpp
    src/host/Commands.cpp
    src/host/CoreHooks.cpp
    src/host/DataSql.cpp
    src/host/DbPolicy.cpp
    src/host/DeferQueue.cpp
    src/host/CreationQueue.cpp
    src/host/HookQueue.cpp
    src/host/EluRouter.cpp
    src/host/EluBattleground.cpp
    src/host/EluMapScripts.cpp
    src/host/EluSocial.cpp
    src/host/Settings.cpp
    src/host/EluHooks.cpp
    src/host/EluWorld.cpp
    src/host/ForeverHost.cpp
    src/host/GameAccess.cpp
    src/host/LuaDb.cpp
    src/host/Module.cpp
    src/host/Scripts.cpp
    src/bind/BindCommon.cpp
    src/bind/BindObjects.cpp
    src/bind/BindWorld.cpp)
  target_include_directories(forever-lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(forever-lua
    PRIVATE
      trinity-core-interface
      mysql
    PUBLIC
      game-interface
      forever-lua-engine)
  trinity_add_module(forever-lua AddSC_forever_lua
    CONFIGS
      ${CMAKE_CURRENT_SOURCE_DIR}/conf/forever_lua.conf.dist)
else()
  # The game-free half of the host side (access rules, deferred work, settings, the event router, the DB reconnect and row
  # reader). The module compiles the same files into forever-lua; here they are what tests/test_host_*.cpp exercise.
  add_library(forever-lua-hostcore STATIC
    src/host/AccessRules.cpp
    src/host/CommandGate.cpp
    src/host/CoreHooks.cpp
    src/host/DataSql.cpp
    src/host/DbPolicy.cpp
    src/host/DeferQueue.cpp
    src/host/CreationQueue.cpp
    src/host/HookQueue.cpp
    src/host/EluRouter.cpp
    src/host/Settings.cpp)
  target_include_directories(forever-lua-hostcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/host ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(forever-lua-hostcore PUBLIC forever-lua-engine)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forever-lua-hostcore PRIVATE -Wall -Wextra)
  endif()
  option(FOREVER_LUA_BUILD_TESTS "Build the standalone unit tests" ON)
  if(FOREVER_LUA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
