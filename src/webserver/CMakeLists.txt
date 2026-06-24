target_sources(dosboxcommon PRIVATE
  bridge.cpp
  capture.cpp
  control.cpp
  cpu.cpp
  dos.cpp
  drive.cpp
  input.cpp
  memory.cpp
  video.cpp
  webserver.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)

target_link_libraries(dosboxcommon PRIVATE
  simde
  base64
  httplib
  json
  PkgConfig::LIBJPEG
  png)
