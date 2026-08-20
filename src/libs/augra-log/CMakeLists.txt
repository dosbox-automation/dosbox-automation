# This file is part of the dosbox-automation Project.
# License: GPL-3.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

# Vendored augra-log (https://github.com/augra-project/augra-log).
# Skip if already provided by vcpkg or a system install.
if(TARGET augra::log)
    return()
endif()

add_library(augra-log STATIC log.cpp)
target_include_directories(augra-log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(augra::log ALIAS augra-log)
