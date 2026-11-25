# Lua 5.4.9 (MIT), vendored unchanged and compiled as C++ (decision D1). Compiled as C++, lua_error / luaL_error unwind
# as C++ exceptions: destructors of C++ frames between the error and the pcall run, and no longjmp crosses a C++ frame.
# lua.c and luac.c (the stand-alone programs) are not built. Never wrap a lua_* call in catch (...).
file(GLOB FOREVER_LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(FILTER FOREVER_LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

add_library(forever_lua_vendored STATIC ${FOREVER_LUA_SOURCES})
set_source_files_properties(${FOREVER_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
set_target_properties(forever_lua_vendored PROPERTIES LINKER_LANGUAGE CXX POSITION_INDEPENDENT_CODE ON)
target_include_directories(forever_lua_vendored SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(UNIX)
  target_compile_definitions(forever_lua_vendored PRIVATE LUA_USE_POSIX)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(forever_lua_vendored PRIVATE -w)
endif()
