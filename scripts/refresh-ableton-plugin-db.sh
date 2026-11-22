#!/usr/bin/env bash
set -euo pipefail

db="${ABLETON_PLUGIN_DB:-"$HOME/Library/Application Support/Ableton/Live Database/Live-plugins-1.db"}"
plugin_name="Aurora Frequency Mod"
plugin_vendor="Aurora Test Bed"

if [[ ! -f "$db" ]]; then
    echo "Ableton plugin database not found; skipping cache refresh: $db" >&2
    exit 0
fi

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "sqlite3 is required to refresh Ableton's plugin database; skipping." >&2
    exit 0
fi

if pgrep -x Live >/dev/null 2>&1; then
    echo "Ableton Live is running; leaving its plugin database untouched." >&2
    echo "Close Live and rerun this script (or the VST build script) to refresh the plugin cache." >&2
    exit 0
fi

table_exists="$(sqlite3 -readonly "$db" \
    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'plugins';")"
if [[ "$table_exists" != "1" ]]; then
    echo "Expected 'plugins' table not found in Ableton database; skipping cache refresh." >&2
    exit 0
fi

required_columns="$(sqlite3 -readonly "$db" \
    "SELECT COUNT(DISTINCT name) FROM pragma_table_info('plugins') WHERE name IN ('name', 'vendor', 'dev_identifier');")"
if [[ "$required_columns" != "3" ]]; then
    echo "Ableton 'plugins' table has an unexpected schema; skipping cache refresh." >&2
    exit 0
fi

matches="$(sqlite3 -readonly "$db" \
    "SELECT COUNT(*) FROM plugins WHERE name = '$plugin_name' AND vendor = '$plugin_vendor' AND dev_identifier GLOB 'device:vst3:audiofx:*';")"
if [[ "$matches" == "0" ]]; then
    echo "No matching Ableton plugin cache entry found; nothing to refresh."
    exit 0
fi

timestamp="$(date '+%Y%m%d-%H%M%S')"
backup="${db}.backup-${timestamp}"
backup_sql="${backup//\'/\'\'}"
sqlite3 "$db" ".backup '$backup_sql'"

if pgrep -x Live >/dev/null 2>&1; then
    echo "Ableton Live started during the refresh; leaving its database untouched." >&2
    echo "A precautionary database backup is available at: $backup" >&2
    exit 0
fi

deleted="$(sqlite3 -batch -noheader "$db" <<SQL
.bail on
BEGIN IMMEDIATE;
DELETE FROM plugins
WHERE name = '$plugin_name'
  AND vendor = '$plugin_vendor'
  AND dev_identifier GLOB 'device:vst3:audiofx:*';
SELECT changes();
COMMIT;
SQL
)"

echo "Removed $deleted matching Ableton plugin cache entr$( [[ "$deleted" == "1" ]] && printf 'y' || printf 'ies' )."
echo "Database backup saved to: $backup"
echo "Ableton will rediscover the VST3 the next time it scans plugins."
