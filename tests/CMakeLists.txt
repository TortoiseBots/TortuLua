# Standalone unit tests of the game-free engine (no ForeverCore). Catch2 v3 (BSL-1.0) is used when installed, else
# fetched at configure time; it is never part of the module build.
include(FetchContent)
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(Catch2)
endif()

add_executable(forever_lua_tests
  FakeHost.cpp
  test_bots.cpp
  test_bots_orders.cpp
  test_budget.cpp
  test_commands.cpp
  test_core_hooks.cpp
  test_db.cpp
  test_errors.cpp
  test_events.cpp
  test_eluna_fixes.cpp
  test_eluna_mode.cpp
  test_eluna_review.cpp
  test_eluna_api.cpp
  test_eluna_e2.cpp
  test_eluna_e3.cpp
  test_eluna_e4.cpp
  test_eluna_e5.cpp
  test_eluna_e45_review.cpp
  test_eluna_creature.cpp
  test_eluna_item.cpp
  test_eluna_object.cpp
  test_eluna_packet.cpp
  test_eluna_player.cpp
  test_eluna_quest_map.cpp
  test_eluna_spell.cpp
  test_eluna_aura_proc.cpp
  test_eluna_scripts.cpp
  test_eluna_unit.cpp
  test_handles.cpp
  test_hardening.cpp
  test_host_access.cpp
  test_host_commands.cpp
  test_host_db.cpp
  test_host_router.cpp
  test_host_writes.cpp
  test_json.cpp
  test_lua_scripts.cpp
  test_memory.cpp
  test_reload.cpp
  test_sandbox.cpp
  test_sqlguard.cpp
  test_text.cpp
  test_timers.cpp)
target_link_libraries(forever_lua_tests PRIVATE forever-lua-hostcore forever-lua-engine Catch2::Catch2WithMain)
target_include_directories(forever_lua_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(forever_lua_tests PRIVATE FL_TEST_LUA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lua"
  FL_TEST_ELUNA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/eluna"
  FL_TEST_SELFTEST_DIR="${CMAKE_SOURCE_DIR}/scripts/selftest")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(forever_lua_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME unit COMMAND forever_lua_tests)
set_tests_properties(unit PROPERTIES TIMEOUT 240)  # a regression that hangs (a loop in C) fails instead of blocking CI
add_test(NAME no_catch_all
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/src -P ${CMAKE_SOURCE_DIR}/scripts/check_no_catch_all.cmake)
add_test(NAME unique_script_names
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/src -P ${CMAKE_SOURCE_DIR}/scripts/check_unique_script_names.cmake)
# E6: the build-time hook detection itself, on a header with and one without the C1-C4 declarations.
add_test(NAME core_hooks
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/hook-detect -P
          ${CMAKE_SOURCE_DIR}/tests/detect_core_hooks.cmake)

# The coverage report (docs/COVERAGE.md, docs/coverage/coverage.json) must list every method, global and event the
# engine registers: a stale report fails here. Python 3 only; the test is skipped when no interpreter is configured.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME coverage_report
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/coverage/gen_coverage.py --check)
  set_tests_properties(coverage_report PROPERTIES TIMEOUT 120)

  # Editor type hints (types/): fail when stale against coverage.json, and run the tool unittests.
  add_test(NAME typehints_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/typehints/gen_typehints.py --check)
  set_tests_properties(typehints_check PROPERTIES TIMEOUT 120)

  add_test(NAME typehints_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/typehints/test_typehints.py)
  set_tests_properties(typehints_test PROPERTIES TIMEOUT 120)
endif()

if(FOREVER_LUA_TSAN)
  # The ThreadSanitizer job runs the bot suites only: they are the ones whose tests drive the world pump and the posting
  # state from two threads. The rest of the suite shares the fake host's maps across test threads, which TSan reports
  # (the fake world is single-threaded by contract), so it is left to the ordinary and ASan/UBSan jobs.
  add_test(NAME bot_threads COMMAND forever_lua_tests "bot*")
  set_tests_properties(bot_threads PROPERTIES TIMEOUT 240)
endif()
