#!/usr/bin/env bash
# Run Moonlight Bench on a Vita, unattended, and compare variants.
#
# Usage: tools/bench/run.sh [options] VARIANT [VARIANT ...]
#
# Each VARIANT is LABEL or LABEL:key=value,key=value, where the keys are
# moonlight.conf settings to use for that variant, for example:
#
#   tools/bench/run.sh base legacy:direct_decode=true big:packetsize=1392
#   tools/bench/run.sh key:bench_input=key mouse:bench_input=mouse pad:bench_input=gamepad
#
# The stats overlay and debug log are turned off for every run, since they
# cost time themselves, unless a variant sets stats_overlay or save_debug_log.
#
# With bench_grab=true, the Vita also saves the decoded frame at fixed page
# ticks. They're fetched next to the reports, and quality.py turns them into
# PNGs and scores picture quality per variant, for example:
#
#   tools/bench/run.sh b10:bitrate=10000,bench_grab=true b20:bitrate=20000,bench_grab=true
#
# Variants run in turn, round after round (base, legacy, big, base, ...), so
# Wi-Fi conditions changing over time affect them all alike.
#
# Options:
#   -s, --seconds N  seconds to measure per run (default 60)
#   -r, --rounds N   rounds, i.e. runs per variant (default 2)
#   -H, --host NAME  paired computer to use (default: the first paired one)
#   -a, --app NAME   Sunshine app showing bench.html (default: Moonlight Bench;
#                    any app with "bench" in its name is also found)
#   -o, --out DIR    where to save reports (default build/bench/<time>)
#
# Needs VITA_IP and a Vita set up as in docs/DEVELOPMENT.md (vitacompanion,
# with Moonlight installed), the host awake with Sunshine running, and the
# benchmark app set up as in tools/bench/README.md. Your moonlight.conf is
# backed up first and restored at the end, even if a run fails.
#
# The whole run holds the Vita lock (tools/vita-lock.sh), so other sessions
# sharing the Vita wait for it to finish. VITA_LOCK=off skips the lock.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BENCH_DIR="$REPO_ROOT/tools/bench"
VITA_SH="$REPO_ROOT/tools/vita.sh"
FTP_PORT="${VITA_FTP_PORT:-1337}"
CMD_PORT="${VITA_CMD_PORT:-1338}"
TITLE_ID="XYZZ00002"
CONF_PATH="ux0:/data/moonlight/moonlight.conf"
REPORT_PATH="ux0:/data/moonlight/reports/last-bench.json"
REPORT_DIR="ux0:/data/moonlight/reports"

seconds=60
rounds=2
host=""
app="Moonlight Bench"
out=""
variants=()
args=("$@")

die() {
  echo "error: $*" >&2
  exit 1
}

usage() {
  awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
  exit 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    -s|--seconds) seconds="$2"; shift 2 ;;
    -r|--rounds) rounds="$2"; shift 2 ;;
    -H|--host) host="$2"; shift 2 ;;
    -a|--app) app="$2"; shift 2 ;;
    -o|--out) out="$2"; shift 2 ;;
    -h|--help) usage ;;
    -*) die "unknown option $1" ;;
    *) variants+=("$1"); shift ;;
  esac
done

[ ${#variants[@]} -gt 0 ] || usage
[ -n "${VITA_IP:-}" ] || die "set VITA_IP to the Vita's address"
[ -x "$VITA_SH" ] || die "tools/vita.sh is missing; it's needed to start and stop the app"
# Run again holding the Vita lock; tools/vita.sh, run from here, sees it's held
if [ -z "${VITA_LOCK_HELD:-}" ] && [ "${VITA_LOCK:-}" != off ]; then
  exec bash "$REPO_ROOT/tools/vita-lock.sh" bash "$BENCH_DIR/run.sh" "${args[@]}"
fi
out="${out:-$REPO_ROOT/build/bench/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$out"

ftp_url() {
  echo "ftp://$VITA_IP:$FTP_PORT/$1"
}

fetch() {
  curl -sf --max-time 20 "$(ftp_url "$1")" -o "$2"
}

upload() {
  curl -sSf --max-time 20 -T "$1" "$(ftp_url "$2")"
}

# Quits Moonlight and waits until it has really exited. A launch sent while the
# previous instance is still closing is ignored, which loses the run.
close_app() {
  local reply
  for _ in $(seq 1 30); do
    reply="$(printf 'quit %s\n' "$TITLE_ID" | nc -w 3 "$VITA_IP" "$CMD_PORT" || true)"
    # vitacompanion answers this once there is nothing left to quit
    case "$reply" in
      *"cannot quit"*) return 0 ;;
    esac
    sleep 0.5
  done
  echo "  warning: Moonlight didn't report closed; launching anyway" >&2
}

# Prints the report's start time and label, to tell a new report from an old one
report_id() {
  python3 -I -c 'import json, sys
try:
    r = json.load(open(sys.argv[1]))
    print(r.get("started", ""), r.get("label", ""))
except Exception:
    print("")' "$1"
}

original="$out/moonlight.conf.orig"
fetch "$CONF_PATH" "$original" || die "can't read $CONF_PATH; is vitacompanion running and Moonlight installed?"
echo "backed up moonlight.conf to $original"

restore() {
  close_app 2>/dev/null || true
  if upload "$original" "$CONF_PATH"; then
    echo "restored the original moonlight.conf"
  else
    echo "warning: couldn't restore moonlight.conf; the backup is $original" >&2
  fi
}
trap restore EXIT

# Fetches the frame grabs a report lists (bench_grab=true), for quality.py
fetch_grabs() {
  local report="$1" name="$2" tick file
  while read -r tick file; do
    [ -n "$tick" ] || continue
    fetch "$REPORT_DIR/$file" "$out/$name-grab-$tick.raw" || echo "  warning: couldn't fetch $file" >&2
  done < <(python3 -I -c 'import json, sys
for g in json.load(open(sys.argv[1])).get("grabs", []):
    print(g["tick"], g["file"])' "$report")
}

run_variant() {
  local spec="$1" round="$2"
  local label="${spec%%:*}"
  local extra=()
  if [ "$spec" != "$label" ]; then
    IFS=',' read -ra extra <<< "${spec#*:}"
  fi

  local conf="$out/$label-$round.conf"
  # The overlay and the debug log cost time themselves, so they're off unless
  # a variant turns them on; variant keys come last and win
  python3 -I "$BENCH_DIR/conf_edit.py" "$original" "$conf" \
    bench_autorun=true "bench_seconds=$seconds" "bench_label=$label" \
    "bench_host=$host" "bench_app=$app" stats_overlay=0 save_debug_log=false \
    ${extra[@]+"${extra[@]}"}

  local before="$out/.before.json"
  rm -f "$before"
  fetch "$REPORT_PATH" "$before" 2>/dev/null || true
  local old_id=""
  [ -f "$before" ] && old_id="$(report_id "$before")"

  close_app
  upload "$conf" "$CONF_PATH"
  "$VITA_SH" launch
  echo "round $round, $label: running for ${seconds}s..."

  # Connecting, waiting for the page, and the reset take some time on top
  local deadline=$(( $(date +%s) + seconds + 150 ))
  local report="$out/$label-$round.json"
  while [ "$(date +%s)" -lt "$deadline" ]; do
    sleep 5
    # Only a report that parses and wasn't there before counts; report_id
    # prints nothing for a file that's missing, partial or corrupt
    local new_id=""
    if fetch "$REPORT_PATH" "$report" 2>/dev/null; then
      new_id="$(report_id "$report")"
    fi
    if [ -n "$new_id" ] && [ "$new_id" != "$old_id" ]; then
      python3 -I -c 'import json, sys
r = json.load(open(sys.argv[1]))
if r.get("ok"):
    print("  input lag p50 %.1f ms, latency est p50 %.1f ms, %.1f fps, %.1f Mbps, %d frames lost, %d grabs" % (
        r["input_lag_ms"]["p50"], r["latency_estimate_ms"]["p50"], r["frames"]["fps_rendered"],
        r["bitrate_mbps"], r["frames"]["lost_network"], len(r.get("grabs", []))))
else:
    print("  failed: %s" % r.get("error"))' "$report"
      fetch_grabs "$report" "$label-$round"
      return 0
    fi
  done
  rm -f "$report"
  echo "  no report within the time limit; check the Vita" >&2
  return 1
}

for round in $(seq 1 "$rounds"); do
  for spec in "${variants[@]}"; do
    run_variant "$spec" "$round" || true
  done
done

shopt -s nullglob
reports=("$out"/*.json)
if [ ${#reports[@]} -gt 0 ]; then
  echo
  python3 -I "$BENCH_DIR/compare.py" "${reports[@]}" | tee "$out/summary.md"
  grabs=("$out"/*-grab-*.raw)
  if [ ${#grabs[@]} -gt 0 ]; then
    echo
    python3 -I "$BENCH_DIR/quality.py" "$out" | tee -a "$out/summary.md" || true
  fi
  echo
  echo "reports and summary in $out"
fi
