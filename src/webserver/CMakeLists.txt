# One unity group: all webserver sources share the same heavy headers
# (httplib, nlohmann/json), so they are parsed once instead of once per file.
# UNITY_BUILD_MODE GROUP combines only sources that have a UNITY_GROUP, so the
# rest of dosboxcommon is compiled as before.
# If the group fails with redefinition errors (same-named static/anonymous
# namespace helpers), add SKIP_UNITY_BUILD_INCLUSION ON to the culprit.
set(WEBSERVER_SOURCES
  bridge.cpp
  capture.cpp
  control.cpp
  cpu.cpp
  dos.cpp
  drive.cpp
  freeze.cpp
  frame_tap.cpp
  input.cpp
  io_port.cpp
  memory.cpp
  mixer.cpp
  video.cpp
  webserver.cpp
  # Keep files that use CHECK_NARROWING() last: its warning pragmas last until
  # the end of the translation unit, so they would affect any file after them.
  tool_launch.cpp
  tools.cpp)

target_sources(dosboxcommon PRIVATE ${WEBSERVER_SOURCES})

# dosboxcommon lives in the top-level directory, so the source properties
# must be set with TARGET_DIRECTORY to be visible to it. Absolute paths avoid
# ambiguity with relative-path resolution.
list(TRANSFORM WEBSERVER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set_source_files_properties(${WEBSERVER_SOURCES}
  TARGET_DIRECTORY dosboxcommon PROPERTIES UNITY_GROUP webserver)
set_target_properties(dosboxcommon PROPERTIES
  UNITY_BUILD ON
  UNITY_BUILD_MODE GROUP)

# find_package instead of pkg-config: works with both system libjpeg
# (Linux) and vcpkg's libjpeg-turbo (Windows, no pkg-config there)
find_package(JPEG REQUIRED)

target_link_libraries(dosboxcommon PRIVATE
  simde
  base64
  httplib
  JPEG::JPEG
  nlohmann_json::nlohmann_json
  PNG::PNG)
