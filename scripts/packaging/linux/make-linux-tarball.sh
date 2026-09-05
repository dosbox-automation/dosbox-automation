#!/bin/bash
# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#
# Relocatable tarball: non-system libraries bundle into bin/lib, where
# the binary's RUNPATH ($ORIGIN/lib) looks.
#
# Usage: make-linux-tarball.sh BUILD_DIR [OUT_DIR] [BUILD_ID] [FLAG]
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

if [ $# -lt 1 ] || [ $# -gt 4 ]; then
    echo "Usage: $0 BUILD_DIR [OUT_DIR] [BUILD_ID] [FLAG]" >&2
    exit 1
fi

build_dir=$(realpath "$1")
out_dir=$(realpath "${2:-.}")
build_id="${3:-}"
# Naming-scheme flag appended to the artifact name, e.g. "compat" for the
# glibc 2.28 build. Distinguishes tiers that otherwise render the same name.
flag="${4:-}"

# The build id becomes part of a path, so keep it to characters that
# cannot escape the staging directory or confuse tar.
case "$build_id" in
    *[!A-Za-z0-9._-]*)
        echo "error: build id '$build_id' may only contain letters, digits, dot, underscore and dash" >&2
        exit 1
        ;;
esac
case "$flag" in
    *[!A-Za-z0-9-]*)
        echo "error: flag '$flag' may only contain letters, digits and dash" >&2
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
# shellcheck disable=SC2016 # matching the literal ${PROJECT_VERSION} text in CMakeLists
suffix=$(sed -n 's/^set(DOSBOX_VERSION \${PROJECT_VERSION}-\([A-Za-z0-9]*\))/\1/p' "$source_dir/CMakeLists.txt")

arch=$(uname -m)
if [ -n "$suffix" ]; then
    name="dosbox-automation-${version}-${suffix}${build_id:+-$build_id}-linux-${arch}${flag:+-$flag}"
else
    name="dosbox-automation-${version}${build_id:+-$build_id}-linux-${arch}${flag:+-$flag}"
fi

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

# FetchContent builds place shared libraries in _deps/<name>-build/.
if [ -d "$build_dir/_deps" ]; then
    while IFS= read -r d; do
        search_path="${search_path:+$search_path:}$d"
    done < <(find "$build_dir/_deps" -name "*.so*" -printf '%h\n' | sort -u)
fi

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

# Bundle libdecor + decoration plugin for Wayland CSD.
# SDL3 dlopens libdecor (not in ldd output), and libdecor dlopens the
# plugin. Without these the tarball has no window decoration on GNOME
# Wayland, and the system GTK3 plugin clashes with our static libpng.
decor_build="$build_dir/src/wayland-decor"
if [ -f "$decor_build/decor-augra.so" ]; then
    mkdir -p "$libdir"
    decor_dir="$stage/$name/bin/lib/libdecor/plugins-1"
    mkdir -p "$decor_dir"

    cp -L --preserve=timestamps "$decor_build/libdecor-0.so.0.2.2" "$libdir/"
    ln -sf libdecor-0.so.0.2.2 "$libdir/libdecor-0.so.0"
    cp --preserve=timestamps "$decor_build/decor-augra.so" "$decor_dir/"
    echo "bundled libdecor-0.so.0 + decor-augra.so"
fi

# Launch wrapper: sets LIBDECOR_PLUGIN_DIR so the bundled decoration
# plugin is found instead of the system GTK3 one.
wrapper="$stage/$name/dosbox-automation"
cat > "$wrapper" << 'WEOF'
#!/bin/sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LIBDECOR_PLUGIN_DIR="$SCRIPT_DIR/bin/lib/libdecor/plugins-1"
export DOSBOX_DECOR_ICON="$SCRIPT_DIR/share/dosbox-automation/icons/png/icon_256.png"
exec "$SCRIPT_DIR/bin/dosbox" "$@"
WEOF
chmod 755 "$wrapper"

cp --preserve=timestamps "$source_dir/scripts/clients/cheat-workbench.sh" "$stage/$name/"

tarball="$out_dir/$name.tar.xz"
tar -C "$stage" -cJf "$tarball" "$name"

echo "created $tarball"
