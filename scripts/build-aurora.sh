#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
aurora_sdk_dir="${AURORA_SDK_DIR:-"$repo_root/lib/Aurora-SDK"}"
gcc_path="${GCC_PATH:-/Library/DaisyToolchain/0.1.2/arm/bin}"

if [[ ! -f "$aurora_sdk_dir/include/aurora.h" \
    || ! -f "$aurora_sdk_dir/libs/libDaisy/core/Makefile" \
    || ! -f "$aurora_sdk_dir/libs/DaisySP/Makefile" ]]; then
    echo "Aurora SDK or its nested dependencies are missing at: $aurora_sdk_dir" >&2
    echo "Run 'git submodule update --init --recursive' or set AURORA_SDK_DIR." >&2
    exit 1
fi

if [[ ! -x "$gcc_path/arm-none-eabi-gcc" || ! -x "$gcc_path/arm-none-eabi-g++" ]]; then
    compiler="$(command -v arm-none-eabi-gcc || true)"
    if [[ -n "$compiler" && -x "$(dirname -- "$compiler")/arm-none-eabi-g++" ]]; then
        gcc_path="$(dirname -- "$compiler")"
    else
        echo "Daisy ARM toolchain not found (arm-none-eabi-gcc and arm-none-eabi-g++)." >&2
        echo "Install the Daisy toolchain, or set GCC_PATH to its bin directory." >&2
        exit 1
    fi
fi

exec make -C "$repo_root" aurora \
    "AURORA_SDK_DIR=$aurora_sdk_dir" \
    "GCC_PATH=$gcc_path"
