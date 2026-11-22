#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
aurora_sdk_dir="${AURORA_SDK_DIR:-$repo_root/lib/Aurora-SDK}"
dfu_timeout="${DFU_TIMEOUT_SECONDS:-120}"
case "${DIAGNOSTIC:-0}" in
    0) firmware_build_dir="build" ;;
    1) firmware_build_dir="build-diagnostic-pass" ;;
    2) firmware_build_dir="build-diagnostic-tone" ;;
    3) firmware_build_dir="build-diagnostic-boost" ;;
    5) firmware_build_dir="build-diagnostic-controls" ;;
    6) firmware_build_dir="build-diagnostic-8x" ;;
    7) firmware_build_dir="build-diagnostic-sdram" ;;
    8) firmware_build_dir="build-diagnostic-controls-cv" ;;
    9) firmware_build_dir="build-diagnostic-usb" ;;
    10) firmware_build_dir="build-diagnostic-playback" ;;
    11) firmware_build_dir="build-diagnostic-record-audit" ;;
    *)
        echo "DIAGNOSTIC must be 0 (Karma looper), 1, 2, 3, 5, 6, 7 (SDRAM test), 8, 9, 10 (playback), or 11 (record audit)." >&2
        exit 2
        ;;
esac
dfu_candidates=()
if [[ -n "${DFU_UTIL:-}" ]]; then
    dfu_candidates+=("$DFU_UTIL")
fi
dfu_candidates+=(/opt/homebrew/bin/dfu-util /usr/local/bin/dfu-util)
path_dfu_util="$(command -v dfu-util || true)"
if [[ -n "$path_dfu_util" ]]; then
    dfu_candidates+=("$path_dfu_util")
fi

dfu_util=""
for candidate in "${dfu_candidates[@]}"; do
    if [[ ! -x "$candidate" ]]; then
        continue
    fi
    if "$candidate" --version >/dev/null 2>&1; then
        dfu_util="$candidate"
        break
    fi
    echo "Ignoring unusable dfu-util at $candidate (its --version check failed)." >&2
done

if [[ -z "$dfu_util" ]]; then
    echo "dfu-util not found. On macOS, install it with: brew install dfu-util" >&2
    echo "If already installed, set DFU_UTIL to its working executable path." >&2
    exit 1
fi

if [[ ! "$dfu_timeout" =~ ^[1-9][0-9]*$ ]]; then
    echo "DFU_TIMEOUT_SECONDS must be a positive integer." >&2
    exit 2
fi

export PATH="$(dirname -- "$dfu_util"):$PATH"
echo "Using dfu-util: $dfu_util"
if [[ "$aurora_sdk_dir" != /* ]]; then
    aurora_sdk_dir="$repo_root/$aurora_sdk_dir"
fi

if [[ "${DIAGNOSTIC:-0}" == 0 ]]; then
    echo "Building Karma stereo looper firmware..."
else
    echo "Building Karma diagnostic ${DIAGNOSTIC}..."
fi
"$repo_root/scripts/build-aurora.sh"

firmware="$repo_root/firmware/$firmware_build_dir/Karma.bin"
if [[ ! -f "$firmware" ]]; then
    echo "Firmware binary not found after build: $firmware" >&2
    exit 1
fi

cat <<EOF

Build complete. Keep the Daisy connected to this computer via its USB device port.
This requires the Daisy bootloader to be installed; STM32 ROM DFU is not sufficient.
Restart the processor with RESET. When the Daisy bootloader's pulsing grace-period
LED pattern starts, press BOOT to keep the bootloader available.
Waiting up to ${dfu_timeout}s for the Daisy bootloader (USB ID 0483:df11)...
EOF

deadline=$((SECONDS + dfu_timeout))
while (( SECONDS < deadline )); do
    device_list="$("$dfu_util" --list 2>&1 || true)"
    if grep -Eiq '0483:df11' <<< "$device_list"; then
        printf '%s\n' "$device_list"
        echo "Flashing Aurora firmware through the Daisy bootloader..."
        make -C "$repo_root/firmware" program-dfu "AURORA_SDK_DIR=$aurora_sdk_dir" "BUILD_DIR=$firmware_build_dir"
        echo "DFU flash complete."
        exit 0
    fi
    sleep 0.25
done

echo "Timed out waiting for the Daisy bootloader." >&2
echo "Check the USB data cable and bootloader mode, then inspect with 'dfu-util --list'." >&2
echo "The STM32 ROM DFU mode is not the Daisy bootloader required for this BOOT_SRAM firmware." >&2
exit 1
