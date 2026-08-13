#!/bin/bash
# This file is part of the dosbox-automation Project.
# License: GPL-3.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
# Relocatable tarball: non-system libraries bundle into bin/lib, where
# the binary's RUNPATH ($ORIGIN/lib) looks.
#
# Usage: make-linux-tarball.sh BUILD_DIR [OUT_DIR] [BUILD_ID]
#
# BUILD_ID goes into the archive name after the version, so a release
# artifact can name the commit it came from like the Windows ones do.

set -euo pipefail

for tool in cmake ldd tar xz realpath sed mktemp cp uname; do
    if ! command -v "$tool" > /dev/null; then
        echo "error: required tool '$tool' not found in PATH" >&2
        exit 1
    fi
done

if [ $# -lt 1 ] || [ $# -gt 3 ]; then
    echo "Usage: $0 BUILD_DIR [OUT_DIR] [BUILD_ID]" >&2
    exit 1
fi

build_dir=$(realpath "$1")
out_dir=$(realpath "${2:-.}")
build_id="${3:-}"

# The build id becomes part of a path, so keep it to characters that
# cannot escape the staging directory or confuse tar.
case "$build_id" in
    *[!A-Za-z0-9._-]*)
        echo "error: build id '$build_id' may only contain letters, digits, dot, underscore and dash" >&2
        exit 1
        ;;
esac

if [ ! -f "$build_dir/CMakeCache.txt" ]; then
    echo "error: $build_dir is not a CMake build directory" >&2
    exit 1
fi
if [ ! -x "$build_dir/dosbox" ]; then
    echo "error: no dosbox binary in $build_dir - build first" >&2
    exit 1
fi

version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")
if [ -z "$version" ]; then
    echo "error: no CMAKE_PROJECT_VERSION in $build_dir/CMakeCache.txt" >&2
    exit 1
fi

# The cache holds the plain project version; the -daN release suffix is a
# non-cache variable, so it has to come from the source CMakeLists.
source_dir=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$build_dir/CMakeCache.txt")
if [ ! -f "$source_dir/CMakeLists.txt" ]; then
    echo "error: source tree '$source_dir' not readable; cannot determine the release suffix" >&2
    exit 1
fi
suffix=$(sed -n 's/^set(DOSBOX_VERSION \${PROJECT_VERSION}-\([A-Za-z0-9]*\))/\1/p' "$source_dir/CMakeLists.txt")
if [ -z "$suffix" ]; then
    echo "error: no DOSBOX_VERSION suffix in $source_dir/CMakeLists.txt" >&2
    exit 1
fi

arch=$(uname -m)
name="dosbox-automation-${version}-${suffix}${build_id:+-$build_id}-linux-${arch}"

# Resolve libraries the way the build was configured, not the way this
# host would: the configure-time CMAKE_PREFIX_PATH lib dirs take
# precedence over the system search path.
prefix_path=$(sed -n 's/^CMAKE_PREFIX_PATH:[^=]*=//p' "$build_dir/CMakeCache.txt")
search_path=""
IFS=';' read -ra prefixes <<< "$prefix_path"
for p in "${prefixes[@]}"; do
    [ -n "$p" ] || continue
    for d in "$p/lib" "$p"/lib/*-linux-gnu*; do
        [ -d "$d" ] && search_path="${search_path:+$search_path:}$d"
    done
done

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

cmake --install "$build_dir" --prefix "$stage/$name" > /dev/null

libdir="$stage/$name/bin/lib"
mkdir -p "$libdir"

bundled=0
while read -r soname arrow path _; do
    [ "$arrow" = "=>" ] && [ -n "$path" ] || continue
    if [ "$path" = "not" ]; then
        echo "error: $soname does not resolve; check the build prefix" >&2
        exit 1
    fi
    case "$path" in
        /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*) continue ;;
    esac
    cp -L --preserve=timestamps "$path" "$libdir/$soname"
    echo "bundled $soname from $path"
    bundled=$((bundled + 1))
done < <(LD_LIBRARY_PATH="$search_path" ldd "$stage/$name/bin/dosbox")

if [ "$bundled" -eq 0 ]; then
    rmdir "$libdir"
    echo "no non-system libraries to bundle"
fi

tarball="$out_dir/$name.tar.xz"
tar -C "$stage" -cJf "$tarball" "$name"

echo "created $tarball"
