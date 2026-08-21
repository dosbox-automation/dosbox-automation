set(VCPKG_TARGET_ARCHITECTURE x64)
# Static CRT to match CMAKE_MSVC_RUNTIME_LIBRARY in the Windows presets:
# the release exe is self-contained, no VC++ redist shipped or required.
# Upstream ships this triplet with dynamic CRT and bundles the redist DLLs.
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
