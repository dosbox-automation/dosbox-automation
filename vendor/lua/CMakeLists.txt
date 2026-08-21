# Vendored Lua 5.5.0 from https://www.lua.org/ftp/lua-5.5.0.tar.gz (MIT).
# Build mirrors the vcpkg lua port: the C library only, C99, no
# interpreter (lua.c) and no compiler (luac.c). Sources are listed
# explicitly so the build is pinned to the audited tarball content.
add_library(lua STATIC
  src/lapi.c
  src/lauxlib.c
  src/lbaselib.c
  src/lcode.c
  src/lcorolib.c
  src/lctype.c
  src/ldblib.c
  src/ldebug.c
  src/ldo.c
  src/ldump.c
  src/lfunc.c
  src/lgc.c
  src/linit.c
  src/liolib.c
  src/llex.c
  src/lmathlib.c
  src/lmem.c
  src/loadlib.c
  src/lobject.c
  src/lopcodes.c
  src/loslib.c
  src/lparser.c
  src/lstate.c
  src/lstring.c
  src/lstrlib.c
  src/ltable.c
  src/ltablib.c
  src/ltm.c
  src/lundump.c
  src/lutf8lib.c
  src/lvm.c
  src/lzio.c
)

target_include_directories(lua SYSTEM PUBLIC src)

set_target_properties(lua PROPERTIES C_STANDARD 99)

if (WIN32)
  target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if (UNIX)
  target_compile_definitions(lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
  find_library(LUA_HAVE_LIBM NAMES m)
  target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS})
  if (LUA_HAVE_LIBM)
    target_link_libraries(lua PRIVATE m)
  endif()
endif()

disable_warnings(lua)
