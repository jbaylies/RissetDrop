#!/bin/sh
# Builds the Linux release for the machine it runs on (x86_64 or aarch64), on
# Linux. Leaves the stripped plugins and app in build-release/linux-<arch>-stage/,
# where scripts/make_release_zips.sh picks them up.
#
#   scripts/package_release_linux.sh
#
# Build on the oldest Linux the release should run on: the binaries need that
# glibc or newer. The release is built on Ubuntu 22.04 (glibc 2.35). Needs
# clang, ninja, CMake 3.31 and YUP's Linux packages.
#
# Building with RISSETDROP_INSTALL_PLUGINS off leaves ~/.vst3, ~/.clap and
# ~/.lv2 alone.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
arch=$(uname -m)
build="$root/build-release/linux-$arch"
stage="$root/build-release/linux-$arch-stage"

cmake -S "$root" -B "$build" -G Ninja \
    -DYUP_DIR="$root/yup" \
    -DCMAKE_BUILD_TYPE=Release \
    -DRISSETDROP_INSTALL_PLUGINS=OFF \
    -DRISSETDROP_BUILD_TESTS=ON
cmake --build "$build"

# The release passes the DSP checks before it is staged
"$build/risset_render_test"

rm -rf "$stage"
mkdir -p "$stage"
cp -R "$build/VST3/Release/RissetDrop.vst3" "$stage/RissetDrop.vst3"
cp -R "$build/LV2/RissetDrop.lv2" "$stage/RissetDrop.lv2"
cp "$build/CLAP/RissetDrop.clap" "$stage/RissetDrop.clap"
cp "$build/RissetDrop" "$stage/RissetDrop"

# Release builds keep their symbols; the release doesn't need them
strip --strip-unneeded \
    "$stage"/RissetDrop.vst3/Contents/*-linux/*.so \
    "$stage"/RissetDrop.lv2/*.so \
    "$stage/RissetDrop.clap" \
    "$stage/RissetDrop"

echo "Staged in $stage"
