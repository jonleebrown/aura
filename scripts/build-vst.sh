#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
juce_source_dir="${JUCE_SOURCE_DIR:-"$repo_root/../lib/JUCE-9.0.4-osx"}"
build_dir="${BUILD_DIR:-"$repo_root/build"}"
install_dir="${VST3_INSTALL_DIR:-/Library/Audio/Plug-Ins/VST3}"
config="${CONFIG:-Release}"

if [[ "$build_dir" != /* ]]; then
    build_dir="$repo_root/$build_dir"
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is required to build the VST3. Install CMake 3.22 or newer." >&2
    exit 1
fi

if [[ ! -f "$juce_source_dir/CMakeLists.txt" ]]; then
    echo "JUCE source not found at: $juce_source_dir" >&2
    echo "Set JUCE_SOURCE_DIR to your JUCE checkout and retry." >&2
    exit 1
fi

make -C "$repo_root" vst \
    "JUCE_SOURCE_DIR=$juce_source_dir" \
    "BUILD_DIR=$build_dir" \
    "CONFIG=$config"

plugin_bundle="$build_dir/vst/KarmaTestBed_artefacts/VST3/Karma Test Bed.vst3"
if [[ ! -d "$plugin_bundle" ]]; then
    echo "VST3 bundle was not created at the expected path: $plugin_bundle" >&2
    exit 1
fi

write_check_dir="$install_dir"
while [[ ! -d "$write_check_dir" ]]; do
    write_check_dir="$(dirname -- "$write_check_dir")"
done

if [[ -w "$write_check_dir" ]]; then
    mkdir -p "$install_dir"
    ditto "$plugin_bundle" "$install_dir/Karma Test Bed.vst3"
else
    if ! command -v sudo >/dev/null 2>&1; then
        echo "Cannot write to $install_dir, and sudo is not available." >&2
        exit 1
    fi
    sudo -v
    sudo mkdir -p "$install_dir"
    sudo ditto "$plugin_bundle" "$install_dir/Karma Test Bed.vst3"
fi

echo "Installed VST3 bundle to: $install_dir/Karma Test Bed.vst3"

# "$repo_root/scripts/refresh-ableton-plugin-db.sh"
