#!/bin/sh
# Builds the macOS release: universal (Apple Silicon and Intel), macOS 11 or
# later, signed ad hoc. Leaves the renamed bundles in build-release/stage/.
#
#   scripts/package_release.sh
#
# Building with RISSETDROP_INSTALL_PLUGINS off leaves the plugins installed in
# ~/Library/Audio/Plug-Ins alone.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build-release"
stage="$build/stage"

cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DRISSETDROP_INSTALL_PLUGINS=OFF \
    -DRISSETDROP_BUILD_TESTS=ON
cmake --build "$build" --config Release -j

# The release passes the DSP checks before it is staged
"$build/risset_render_test"

rm -rf "$stage"
mkdir -p "$stage"
ditto "$build/rissetdrop_au_plugin.component" "$stage/RissetDrop.component"
ditto "$build/VST3/Release/rissetdrop_vst3_plugin.vst3" "$stage/RissetDrop.vst3"
ditto "$build/rissetdrop_clap_plugin.clap" "$stage/RissetDrop.clap"
ditto "$build/RissetDrop.app" "$stage/RissetDrop.app"

# YUP's bundles carry signatures that no longer match their contents
for bundle in "$stage"/*; do
    codesign --force --deep --sign - "$bundle"
    codesign --verify --strict "$bundle"
done

echo "Staged in $stage"
