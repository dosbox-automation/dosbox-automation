target_sources(dosboxcommon PRIVATE
  bridge.cpp
  capture.cpp
  control.cpp
  cpu.cpp
  dos.cpp
  dosbox.cpp
  drive.cpp
  input.cpp
  memory.cpp
  video.cpp
  webserver.cpp)

# find_package instead of pkg-config: works with both system libjpeg
# (Linux) and vcpkg's libjpeg-turbo (Windows, no pkg-config there)
find_package(JPEG REQUIRED)

target_link_libraries(dosboxcommon PRIVATE
  simde
  base64
  httplib
  json
  JPEG::JPEG
  PNG::PNG)
