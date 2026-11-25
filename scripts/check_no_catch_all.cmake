# A catch (...) around a lua_* call would swallow Lua's C++ unwinding (decision D1). None is allowed in src/.
# Usage: cmake -DSRC=<src folder> -P check_no_catch_all.cmake
file(GLOB_RECURSE files ${SRC}/*.cpp ${SRC}/*.h)
set(bad "")
foreach(f ${files})
  file(STRINGS ${f} hits REGEX "catch[ \t]*\\([ \t]*\\.\\.\\.[ \t]*\\)")
  if(hits)
    list(APPEND bad "${f}: ${hits}")
  endif()
endforeach()
if(bad)
  message(FATAL_ERROR "catch (...) found:\n${bad}")
endif()
message(STATUS "no catch (...) in ${SRC}")
