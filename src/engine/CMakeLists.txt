# The game-free engine: Lua state, sandbox, budget, timers, queue, handles, SQL guard, JSON, command tail.
# It never includes a core header, so it builds (and is unit-tested) without ForeverCore.
add_library(forever-lua-engine STATIC
  Api.cpp
  BotHub.cpp
  BotProvider.cpp
  CommandTail.cpp
  EluAlias.cpp
  Engine.cpp
  EventQueue.cpp
  GuidText.cpp
  Handle.cpp
  Json.cpp
  Sandbox.cpp
  SqlGuard.cpp
  StateSet.cpp
  Text.cpp
  Timers.cpp
  elu/EluCommon.cpp
  elu/EluBattleGround.cpp
  elu/EluBot.cpp
  elu/EluCorpse.cpp
  elu/EluCreature.cpp
  elu/EluGameObject.cpp
  elu/EluGlobal.cpp
  elu/EluGossip.cpp
  elu/EluGame.cpp
  elu/EluGroup.cpp
  elu/EluGuild.cpp
  elu/EluItem.cpp
  elu/EluQuest.cpp
  elu/EluSpell.cpp
  elu/EluSpellInfo.cpp
  elu/EluAuraEffect.cpp
  elu/EluProcInfo.cpp
  elu/EluObject.cpp
  elu/EluPlayer.cpp
  elu/EluPacket.cpp
  elu/EluQuery.cpp
  elu/EluStubs.cpp
  elu/EluUnit.cpp)
target_include_directories(forever-lua-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(forever-lua-engine PUBLIC cxx_std_17)
target_link_libraries(forever-lua-engine PUBLIC forever_lua_vendored)
find_package(Threads REQUIRED)
target_link_libraries(forever-lua-engine PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(forever-lua-engine PRIVATE -Wall -Wextra)
endif()
