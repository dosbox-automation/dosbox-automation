cmake_minimum_required(VERSION 3.25)

# The project only tests preset build configurations, so we use
# the IS_PRESET_USED variable to check and recommend using a preset.

if (NOT IS_PRESET_USED)
  message(WARNING
    "\nPer docs/build-*.md, we recommend using a platform-specific preset.
     cmake --preset <name>"
  )
  execute_process(
    COMMAND ${CMAKE_COMMAND} --list-presets
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  message("")
endif()

set(VCPKG_USE_HOST_TOOLS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for use with LSPs")

project(dosbox-automation
  LANGUAGES C CXX
  VERSION 0.84.0
)

set(DOSBOX_VERSION ${PROJECT_VERSION}-da2)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

# ccache support
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  # Direct mode is 10-fold quicker than non-direct mode
  set(ENV{CCACHE_DIRECT} "true")
  message(STATUS "Found and using ccache")
endif()

# Disable the noisy narrowing and conversion warnings by default. Use the
# CHECK_NARROWING() macro to opt-in these checks on a per-file basis. See
# `src/util/checks.h` for details.
#
# Warning flags are set globally via add_compile_options() rather than
# per-target because the project wants consistent diagnostics across all
# compiled code. Vendored third-party libraries under src/libs/ opt out
# via disable_warnings() which applies -w to suppress all warnings.
if (MSVC)
  # /W4 displays level 1, level 2, and level 3 warnings, and all level 4
  # (informational) warnings that aren't off by default.
  add_compile_options("/wd4244" "/MP" "/W4")

  # Disable cl/clangcl warnings about insecure C functions
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

  # vcpkg builds Lua 5.5 as C (the port's cpp feature is off), so lua_yield
  # and luaL_error leave a C function through setjmp/longjmp. On Windows
  # MSVC's longjmp unwinds C++ frames, and the default /EHsc includes /EHc,
  # which tells the compiler extern "C" functions never throw - so it drops
  # the unwind bookkeeping around calls to the (extern "C") Lua entry
  # points. The longjmp then unwinds the yielding frame through that stale
  # state and double-frees a local: deterministic heap corruption on
  # wait_for_text/type (aug-zj8l). /EHs is /EHsc without the extern-C-nothrow
  # assumption; unwind stays correct. This corrects every luaL_error and
  # lua_yield unwind site, not just those two functions.
  # Replace the default rather than append so /EHsc does not linger.
  if (CMAKE_CXX_FLAGS MATCHES "/EHsc")
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  elseif (NOT CMAKE_CXX_FLAGS MATCHES "/EHs")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHs")
  endif()
  message(STATUS "aug-zj8l: MSVC C++ exception model set to /EHs")

else()

  # Warning flags common to C and C++ code
  set(WARNING_FLAGS_COMMON
      "-Wall" "-Wextra" "-Wdeprecated"
      "-Wno-unknown-warning-option" "-Wno-unknown-pragmas"
      "-Wno-conversion" "-Wno-narrowing"
      "-Wdisabled-optimization" "-Wduplicated-branches" "-Wtrampolines" "-Wvla"
      "-Wlogical-op" "-Wduplicated-cond" "-Wpointer-arith"
      "-Wredundant-decls" "-Wfloat-conversion"
  )

  # Warning flags only valid for the C code
  set(WARNING_FLAGS_C
      ${WARNING_FLAGS_COMMON}
      "-Wjump-misses-init"
      "-Wnested-externs"
      "-Wold-style-definition"
      "-Wstrict-prototypes"
      "-Wvariadic-macros"
  )

  # Warning flags only valid for the C++ code
  set(WARNING_FLAGS_CXX
      ${WARNING_FLAGS_COMMON}
      "-Weffc++"
      "-Wextra-semi"
      "-Wimplicit-fallthrough"
  )

  # GCC 12 (still used in our CI workflow) complains about unrecognized
  # command line options, so we'll enable these conditionally for the GCC 13+
  # and Clang builds only for now.
  #
  if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
       OR
       (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))

    set(WARNING_FLAGS_C
        ${WARNING_FLAGS_C}
        "-Winvalid-utf8"
        "-Wtrivial-auto-var-init"
    )
    set(WARNING_FLAGS_CXX
        ${WARNING_FLAGS_CXX}
        "-Winvalid-utf8"
        "-Wtrivial-auto-var-init"
    )
  endif()

  if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
       OR
       (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))

    set(WARNING_FLAGS_C
            ${WARNING_FLAGS_C}
            "-Wdeprecated-non-prototype"
            "-Wflex-array-member-not-at-end"
            "-Wfree-labels"
    )
  endif()

  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:${WARNING_FLAGS_C}>"
    "$<$<COMPILE_LANGUAGE:CXX>:${WARNING_FLAGS_CXX}>"
  )

endif()

# GCC needs more memory reserved to fully optimize the CPU emulation code
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options("--param" "max-gcse-memory=300000")
endif()

# Project decision: release builds use -O2 instead of CMake's -O3
# default. MSVC's own /O2 release default already matches.
if (NOT MSVC)
  set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

if (APPLE)
  # TODO: These are sprintf deprecated warnings. We probably do want to remove
  # sprintf usage at some point.
  add_compile_options("-Wno-deprecated-declarations")

  # This adds more metadata to the executable (about ~500KB) which is handy
  # for profiling. It also results in better crash dumps. This does not affect
  # performance at all.
  add_compile_options("-g")

  if (CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    # This has virtually zero performance impact on ARM and it gives us deep
    # observability on release mode builds at full speed (e.g., when using a
    # profiler).
    add_compile_options("-fno-omit-frame-pointer")
  endif()
endif()

option(OPT_DEBUGGER "Enable debugger" OFF)
option(OPT_HEAVY_DEBUGGER "Enable heavy debugger" OFF)
if (OPT_HEAVY_DEBUGGER)
  set(OPT_DEBUGGER ON CACHE INTERNAL "")
endif()

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckCXXSourceCompiles)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(GNUInstallDirs)

if (WIN32)
  set(DOSBOX_PLATFORM_WINDOWS ON)

elseif (APPLE)
  set(DOSBOX_PLATFORM_MACOS ON)
  set(MACOSX ON)

elseif (LINUX)
  set(DOSBOX_PLATFORM_LINUX ON)

else()
  message(FATAL_ERROR "Unknown system ${CMAKE_SYSTEM_NAME}")
endif()

# Sanitizers
option(OPT_SANITIZER        "Enable general sanitizer checks")
option(OPT_THREAD_SANITIZER "Enable thread sanitizer")

if (OPT_SANITIZER AND OPT_THREAD_SANITIZER)
  message(FATAL_ERROR
          "-DOPT_SANITIZER and -DOPT_THREAD_SANITIZER are mutually exclusive")
endif()

if (OPT_SANITIZER OR OPT_THREAD_SANITIZER)
  if (MSVC)
    # The MSVC/clang-cl toolchain only supports AddressSanitizer on Windows;
    # the thread, leak, and undefined-behaviour sanitizers are unavailable.
    if (OPT_THREAD_SANITIZER)
      message(FATAL_ERROR "The thread sanitizer is not supported by the MSVC/clang-cl toolchain; only OPT_SANITIZER (AddressSanitizer) is available on Windows")
    endif()

    # AddressSanitizer is incompatible with edit-and-continue debug info,
    # incremental linking, and the /RTC runtime checks. CMake enables none of
    # these by default, but we pin the debug info format and disable
    # incremental linking to be safe.
    # See: https://learn.microsoft.com/en-us/cpp/sanitizers/asan
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")
    add_compile_options("/fsanitize=address")
    add_link_options("/INCREMENTAL:NO")

  else()
    # GCC and Clang
    if (OPT_SANITIZER)
      if (DOSBOX_PLATFORM_MACOS)
        set(SANITIZER_FLAGS "address,pointer-compare,pointer-subtract,undefined")
      else()
        set(SANITIZER_FLAGS "address,pointer-compare,pointer-subtract,leak,undefined")
      endif()
    else()
      set(SANITIZER_FLAGS "thread")
    endif()

    # Disable optimizations, increase amount of debug info
    add_compile_options("-O0")
    add_compile_options("-fno-omit-frame-pointer")
    add_compile_options("-g")

    # Enable selected sanitizer mechanisms
    add_compile_options("-fsanitize=${SANITIZER_FLAGS}")
    add_link_options("-fsanitize=${SANITIZER_FLAGS}")

    # Ask to recover sanitizer output
    add_compile_options("-fsanitize-recover=all")
  endif()
endif()

# Diagnostic option: emit a PDB for the release binary without changing
# codegen. clang-cl /Zi emits CodeView into the objects (no codegen
# difference); /DEBUG makes the linker produce dosbox.pdb. /DEBUG also
# flips the linker defaults to /OPT:NOREF,NOICF, which would change the
# binary layout, so both are re-pinned to keep the release image
# identical (matters for heap-layout-sensitive bugs, see aug-zj8l).
option(OPT_PDB "Emit PDB debug info for release diagnostics (Windows ClangCL)")
if (OPT_PDB)
  if (NOT MSVC)
    message(FATAL_ERROR "OPT_PDB is only for the Windows ClangCL build")
  endif()
  add_compile_options(/Zi)
  add_link_options(/DEBUG /OPT:REF /OPT:ICF)
endif()

# Check host endianness
if (CMAKE_CXX_BYTE_ORDER STREQUAL BIG_ENDIAN)
  message(FATAL_ERROR "Big-endian architectures are not supported")
endif()

# Enable color output for certain compilers
set(CMAKE_COLOR_DIAGNOSTICS ON)

if (NOT BUILD_GIT_HASH)
  find_package(Git)
  if (Git_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short=5 HEAD
      WORKING_DIRECTORY         ${CMAKE_CURRENT_LIST_DIR}
      OUTPUT_VARIABLE           BUILD_GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
  if (NOT BUILD_GIT_HASH)
    set(BUILD_GIT_HASH "?")
  endif()
endif()

check_include_file("unistd.h" HAS_UNISTD)
if (NOT HAS_UNISTD)
  configure_file(
    src/platform/windows/visualc/unistd.h
    ${CMAKE_CURRENT_BINARY_DIR}/include/unistd.h
  )
endif()

# File-descriptor manipulation routines, such as FD_ZERO, are used
# by Enet, slirp, and ManyMouse's X11 interface. Unfortunately these
# routines aren't universally available.
if (DOSBOX_PLATFORM_WINDOWS)
  check_symbol_exists(FD_ZERO "winsock2.h"   HAVE_FD_ZERO)
else()
  check_symbol_exists(FD_ZERO "sys/select.h" HAVE_FD_ZERO)
endif()

set(TEST_CODE_BUILTIN_AVAILABLE "
int main() {
    if (__builtin_available(macOS 11, *)) {
        return 0;
    }
    return 0;
}
")

check_cxx_source_compiles("${TEST_CODE_BUILTIN_AVAILABLE}"
                          HAVE_BUILTIN_AVAILABLE)

set(TEST_CODE_BUILTIN_CLEAR_CACHE "
int main() {
    char buffer[10];
    __builtin___clear_cache(buffer, buffer + 10);
    return 0;
}
")

check_cxx_source_compiles("${TEST_CODE_BUILTIN_CLEAR_CACHE}"
                          HAVE_BUILTIN_CLEAR_CACHE)

set(C_TARGET_CPU_ARM    OFF)
set(C_TARGET_CPU_X86    OFF)

option(OPT_FORCE_DYNREC "Force DYNREC dynamic recompiler (not x86-64 optimized)" OFF)

if (CMAKE_SYSTEM_PROCESSOR  STREQUAL "AMD64"  OR
    CMAKE_SYSTEM_PROCESSOR  STREQUAL "x86_64" OR
    CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")

  set(C_TARGET_CPU_X86    ON)

  if (OPT_FORCE_DYNREC)
      set(C_DYNAMIC_X86  OFF)
      set(C_DYNREC       ON)
  else()
      set(C_DYNAMIC_X86  ON)
      set(C_DYNREC       OFF)
  endif()

  set(C_FPU_X86          ON)
  set(C_UNALIGNED_MEMORY ON)

elseif (CMAKE_SYSTEM_PROCESSOR  STREQUAL "arm64"   OR
        CMAKE_SYSTEM_PROCESSOR  STREQUAL "aarch64" OR
        CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")

  set(C_TARGET_CPU_ARM    ON)

  set(C_DYNAMIC_X86      OFF)
  set(C_DYNREC           ON)
  set(C_FPU_X86          OFF)
  set(C_UNALIGNED_MEMORY OFF)

else()
  message(FATAL_ERROR "Unknown processor ${CMAKE_SYSTEM_PROCESSOR}")
endif()

check_include_file("libgen.h"     HAVE_LIBGEN_H)
check_include_file("netinet/in.h" HAVE_NETINET_IN_H)
check_include_file("stdlib.h"     HAVE_STDLIB_H)
check_include_file("strings.h"    HAVE_STRINGS_H)
check_include_file("sys/types.h"  HAVE_SYS_TYPES_H)
check_include_file("sys/xattr.h"  HAVE_SYS_XATTR_H)

check_symbol_exists(getpeername  sys/socket.h  HAVE_GETPEERNAME)
check_symbol_exists(getsockname  sys/socket.h  HAVE_GETSOCKNAME)
if (HAVE_GETPEERNAME AND HAVE_GETSOCKNAME)
  set(HAVE_SYS_SOCKET_H ON)
endif()

set(C_PER_PAGE_W_OR_X ON)

option(OPT_OPENGL "Enable OpenGL render backend" ON)
if (OPT_OPENGL)
  set(C_OPENGL ON)
endif()

option(OPT_MT32EMU "Enable Roland MT-32 emulation support" ON)
if (OPT_MT32EMU)
  set(C_MT32EMU ON)
  find_package(MT32Emu REQUIRED)
endif()

if (OPT_DEBUGGER)
  set(C_DEBUGGER ON)

  if (OPT_HEAVY_DEBUGGER)
    set(C_HEAVY_DEBUGGER ON)
  endif()
endif()

# ManyMouse - optional XInput support
option(OPT_MANYMOUSE
  "Use ManyMouse library for single-computer multiplayer gaming in The Settlers I/II"
  ON
)

# ManyMouse - optional XInput support
option(OPT_XINPUT
  "Let ManyMouse use the X Input protocol"
  OFF
)

# ManyMouse
if (OPT_MANYMOUSE)
  if (NOT HAVE_FD_ZERO)
    message(WARNING "ManyMouse requires FD_ZERO support")

  elseif (DOSBOX_PLATFORM_MACOS)
    find_library(IOKit_LIBRARY IOKit)
    if (NOT IOKit_LIBRARY)
      message(WARNING "ManyMouse requires IOKit")
    else()
      set(C_MANYMOUSE ON)
    endif()

  else()
    set(C_MANYMOUSE ON)
  endif()

  if (C_MANYMOUSE AND OPT_XINPUT)
    check_include_file("X11/extensions/XInput2.h" HAVE_XINPUT)
    include (${CMAKE_ROOT}/Modules/FindX11.cmake)
    if ((NOT ${X11_Xinput_FOUND}) OR (NOT HAVE_XINPUT))
      message(WARNING "XInput not found.")
    else()
      set(SUPPORT_XINPUT2 ON)
    endif()
  endif()
endif()

# Tests
option(OPT_TESTS "Enable tests" ON)

# Offline documentation

if (OPT_TESTS)
  enable_testing()
endif()

# Linux
set(C_ALSA "${DOSBOX_PLATFORM_LINUX}")

# Windows
if (DOSBOX_PLATFORM_WINDOWS)
  # Prevent <windows.h> from clobbering std::min and std::max
  set(NOMINMAX ON)
  # Enable mathematical constants (such as M_PI) in Windows math.h header
  set(_USE_MATH_DEFINES ON)
endif()

check_symbol_exists(strnlen       "string.h"       HAVE_STRNLEN)
check_symbol_exists(clock_gettime "time.h"         HAVE_CLOCK_GETTIME)
check_symbol_exists(mprotect      "sys/mman.h"     HAVE_MPROTECT)
check_symbol_exists(mmap          "sys/mman.h"     HAVE_MMAP)
check_symbol_exists(MAP_JIT       "sys/mman.h"     HAVE_MAP_JIT)
check_symbol_exists(setpriority   "sys/resource.h" HAVE_SETPRIORITY)

check_symbol_exists(
  pthread_setname_np "pthread.h" HAVE_PTHREAD_SETNAME_NP
)
check_symbol_exists(
  pthread_jit_write_protect_np "pthread.h" HAVE_PTHREAD_WRITE_PROTECT_NP
)
check_symbol_exists(
  sys_icache_invalidate "libkern/OSCacheControl.h" HAVE_SYS_ICACHE_INVALIDATE
)

check_struct_has_member(
  "struct dirent"
  d_type
  dirent.h
  HAVE_STRUCT_DIRENT_D_TYPE
)

set(CUSTOM_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}")

set(project_name "${PROJECT_NAME}")
set(version      "${PROJECT_VERSION}")

# PkgConfig is needed for speexdsp (no cmake config in vcpkg).
find_package(PkgConfig REQUIRED)

# Plain zlib for the ZMBV video codec. zlib-ng is not worth the extra
# dependency here; ZMBV compression is not a bottleneck. C_ZLIB_NG stays
# unset, so the upstream zmbv.h conditional resolves to the zlib API.
find_package(ZLIB REQUIRED)
set(ZMBV_ZLIB_TARGET ZLIB::ZLIB)

configure_file(
  src/dosbox_config.h.in.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/include/dosbox_config.h
)

# Set RPATH so runtime-loaded shared libraries (e.g., slirp) can be
# found in a lib/ directory next to the executable. Windows uses its
# own DLL search path (exe directory), so RPATH is not needed there.
if (APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path/lib")
elseif (DOSBOX_PLATFORM_LINUX)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
endif()
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# Find common external dependencies
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
find_package(SDL3_image CONFIG REQUIRED)
find_package(iir REQUIRED)
find_package(PNG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(FluidSynth REQUIRED CONFIG)
if (C_ALSA)
  find_package(ALSA REQUIRED)
endif()

# SpeexDSP: vcpkg has no cmake config, only a .pc file
pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET speexdsp)

# Lua 5.5 scripting engine and sol2 C++ bindings
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_executable(dosbox src/main.cpp src/dosbox.cpp)
if (OPT_DEBUGGER)
  set_target_properties(dosbox PROPERTIES OUTPUT_NAME dosbox_with_debugger)
endif()
if (WIN32)
  target_sources(dosbox PRIVATE src/dosbox.rc)
endif()

set(LICENSE_FILES
  THIRD_PARTY_LICENSES.txt
)

# The source of the resources, relative to the source root
set(RESOURCES_PATH "resources")

# The copy target of the resources, relative to the compiled binary
if (APPLE OR WIN32)
  set(RESOURCE_COPY_PATH "Resources" CACHE STRING "Resources copy target relative to binary")
else()
  set(RESOURCE_COPY_PATH "resources" CACHE STRING "Resources copy target relative to binary")
endif()

# Generate asset copy commands and add them as a dependency
include(cmake/add_copy_assets.cmake)
add_copy_assets()

# Project-wide include paths used by the main library and standalone
# libraries that reference project headers (dosbox_config.h, etc.).
# Using an INTERFACE library makes these paths explicit per-target
# instead of polluting every target via global include_directories().
add_library(project_headers INTERFACE)
target_include_directories(project_headers INTERFACE
  src
  ${CMAKE_CURRENT_BINARY_DIR}/include
)

add_library(dosboxcommon STATIC)
target_link_libraries(dosboxcommon PUBLIC project_headers)


# Add tests before src so that the dosbox_tests target is
# available for target_sources()
if (OPT_TESTS)
  add_subdirectory(tests)
endif()

add_subdirectory(src)

# libatomic is part of the GCC runtime library.
# This is used by GCC and Clang by default on Linux.
# Mac and Windows don't need this except for maybe MSYS2.
# This is required for Linux to use all features of std::atomic.
#
# The versioned libatomic.so.1 fallback is needed because some Linux
# environments only ship the runtime library without the unversioned
# libatomic.so dev symlink that find_library(NAMES atomic) expects.
find_library(LIBATOMIC NAMES atomic libatomic.so.1)

if (LIBATOMIC)
  target_link_libraries(dosboxcommon PRIVATE ${LIBATOMIC})
endif()

target_link_libraries(dosboxcommon PUBLIC loguru)
target_link_libraries(dosboxcommon PRIVATE
  $<IF:$<TARGET_EXISTS:iir::iir>,iir::iir,iir::iir_static>
  decoders
)

target_link_libraries(dosbox PRIVATE
  dosboxcommon
  SDL3::Headers
)

# Export dynamic symbols so loguru's stack traces can resolve names
if (HAS_STACK_INCLUDES)
  target_link_options(dosbox PRIVATE "-rdynamic")
endif()

# Add installation rules
include(cmake/add_install_rules.cmake)
add_install_rules()
