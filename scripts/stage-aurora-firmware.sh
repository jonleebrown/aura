#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mount_path="${1:-${AURORA_USB_MOUNT:-}}"
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
    *)
        echo "DIAGNOSTIC must be 0 (Karma looper), 1, 2, 3, 5, 6, 7 (SDRAM test), 8, or 9." >&2
        exit 2
        ;;
esac
firmware="$repo_root/firmware/$firmware_build_dir/Karma.bin"

if [[ -z "$mount_path" ]]; then
    echo "Usage: $0 /Volumes/<Aurora USB volume>" >&2
    echo "Or set AURORA_USB_MOUNT to the mounted USB volume path." >&2
    exit 2
fi

if [[ ! -f "$firmware" ]]; then
    echo "Firmware binary not found: $firmware" >&2
    echo "Build it first with ./scripts/build-aurora.sh." >&2
    exit 1
fi

if [[ ! -d "$mount_path" ]]; then
    echo "USB volume is not mounted or path does not exist: $mount_path" >&2
    exit 1
fi

mount_path="$(cd "$mount_path" && pwd -P)"
if ! /sbin/mount | grep -Fq " on $mount_path ("; then
    echo "Path is not the root of a mounted volume: $mount_path" >&2
    exit 1
fi

destination="$mount_path/Karma.bin"
other_bins=()
while IFS= read -r -d '' candidate; do
    if [[ "$candidate" != "$destination" ]]; then
        other_bins+=("$candidate")
    fi
done < <(find "$mount_path" -maxdepth 1 -type f -name '*.bin' -print0)

if (( ${#other_bins[@]} > 0 )); then
    echo "Aurora expects a single .bin file in the USB volume root." >&2
    echo "Move these files into a folder before staging this firmware:" >&2
    printf '  %s\n' "${other_bins[@]}" >&2
    exit 1
fi

cp "$firmware" "$destination"
sync
echo "Staged firmware at: $destination"
echo "Safely eject the USB volume, insert it into Aurora, and power-cycle the module to load the firmware."
