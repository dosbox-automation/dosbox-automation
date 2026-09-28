# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

# B-Con's crypto-algorithms (https://github.com/B-Con/crypto-algorithms),
# public domain per its README: the identity hash needs SHA-256 without OpenSSL.
add_library(sha256 STATIC sha256.c)
target_include_directories(sha256 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
disable_warnings(sha256)
