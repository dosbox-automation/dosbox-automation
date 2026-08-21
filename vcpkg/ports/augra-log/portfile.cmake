vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO augra-project/augra-log
    REF c0b71635383a3bcf1cb56109424a1907a664f989
    SHA512 0
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DAUGRA_LOG_BUILD_TESTS=OFF
        -DAUGRA_LOG_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME augra-log CONFIG_PATH lib/cmake/augra-log)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
