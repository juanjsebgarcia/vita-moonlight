#!/usr/bin/env bash
# Build Vita Moonlight in Docker and deploy it to a Vita running vitacompanion.
#
# Usage: tools/vita.sh <command> [args]
#
#   build          Build build/eboot.bin and build/moonlight.vpk in the VitaSDK image
#   push           Stop the app, upload build/eboot.bin, and launch it again
#   launch         Launch the installed app
#   kill           Stop the running app
#   log [FILE]     Fetch moonlight.log from the Vita (prints it, or saves to FILE)
#   crash          Fetch the newest core dump from ux0:data
#   all            build, then push
#
# Environment:
#   VITA_IP        Address of the Vita (required for everything except build)
#   VITA_FTP_PORT  vitacompanion FTP port (default 1337)
#   VITA_CMD_PORT  vitacompanion command port (default 1338)
#   VITASDK_IMAGE  Docker image to build with (default: the pinned image below)
#   VITA_OPT_LEVEL Passed to CMake, e.g. -O0 for a debugging build
#
# One-time setup on the Vita: install the VPK once with VitaShell, then load
# vitacompanion (https://github.com/devnoname120/vitacompanion) under *main in
# ur0:tai/config.txt and reboot. After that, `push` only replaces eboot.bin.
# Changes to sce_sys/ or assets/ still need the VPK reinstalled.

set -euo pipefail

TITLE_ID="XYZZ00002"
# Pinned so builds are reproducible. Update deliberately, and check the build
# still links, since SDK package changes have broken it before.
DEFAULT_IMAGE="vitasdk/vitasdk@sha256:8227da1b596d981f4513cd9b36bda1e43857f6dae5bcf4312c1cd40167f7d9bb"
IMAGE="${VITASDK_IMAGE:-$DEFAULT_IMAGE}"
FTP_PORT="${VITA_FTP_PORT:-1337}"
CMD_PORT="${VITA_CMD_PORT:-1338}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() {
  echo "error: $*" >&2
  exit 1
}

require_vita() {
  [ -n "${VITA_IP:-}" ] || die "set VITA_IP to the Vita's address"
  local port
  for port in "$FTP_PORT" "$CMD_PORT"; do
    nc -z -w 3 "$VITA_IP" "$port" 2>/dev/null \
      || die "nothing listening on $VITA_IP:$port; is vitacompanion loaded and the Vita awake?"
  done
}

# vitacompanion takes one command per connection, terminated by a newline, and
# replies with a line of text. `help` lists the commands it supports. A failed
# connection is fatal, so push never uploads over an app it couldn't stop.
vita_cmd() {
  printf '%s\n' "$1" | nc -w 3 "$VITA_IP" "$CMD_PORT" \
    || die "couldn't send '$1' to $VITA_IP:$CMD_PORT"
}

ftp_url() {
  echo "ftp://$VITA_IP:$FTP_PORT/$1"
}

cmd_build() {
  # The image's libcurl is built against OpenSSL 1.0.2, but the image ships
  # 1.1.1 (pulled in by cpython3). Swap to 1.0.2 inside the throwaway container.
  docker run --rm \
    -v "$REPO_ROOT:/src" -w /src \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -e VITA_OPT_LEVEL="${VITA_OPT_LEVEL:-}" \
    "$IMAGE" sh -ec '
      vdpm pacman -- -Rdd --noconfirm openssl-1.1.1 >/dev/null 2>&1 || true
      vdpm pacman -- -S --noconfirm openssl >/dev/null
      opt=""
      if [ -n "$VITA_OPT_LEVEL" ]; then opt="-DVITA_OPT_LEVEL=$VITA_OPT_LEVEL"; fi
      cmake -S . -B build $opt >/dev/null
      make -C build -j"$(nproc)"
      chown -R "$HOST_UID:$HOST_GID" build
    '
  ls -l "$REPO_ROOT/build/eboot.bin" "$REPO_ROOT/build/moonlight.vpk"
}

cmd_kill() {
  require_vita
  local reply
  reply="$(vita_cmd "quit $TITLE_ID")"
  # vitacompanion reports an error when the app is not running, which is fine here
  case "$reply" in
    *"cannot quit"*) ;;
    *[Ee]rror*) echo "vitacompanion: $reply" >&2 ;;
  esac
}

cmd_launch() {
  require_vita
  local reply
  reply="$(vita_cmd "launch $TITLE_ID")"
  case "$reply" in
    *[Ee]rror*) die "vitacompanion: $reply" ;;
  esac
}

cmd_push() {
  require_vita
  local eboot="$REPO_ROOT/build/eboot.bin"
  [ -f "$eboot" ] || die "no $eboot; run '$0 build' first"
  curl -sf --max-time 10 "$(ftp_url "ux0:/app/$TITLE_ID/")" >/dev/null \
    || die "ux0:app/$TITLE_ID is not installed; install build/moonlight.vpk once with VitaShell"
  cmd_kill
  # The Vita refuses the write (FTP 550) while the app still has eboot.bin
  # open, so retry for a few seconds while it exits
  local attempt err
  for attempt in 1 2 3 4 5 6 7 8; do
    if err="$(curl -sS --max-time 60 -T "$eboot" "$(ftp_url "ux0:/app/$TITLE_ID/eboot.bin")" 2>&1)"; then
      echo "uploaded $(wc -c < "$eboot" | tr -d ' ') bytes"
      cmd_launch
      return 0
    fi
    sleep 1
  done
  die "upload failed: $err"
}

cmd_log() {
  require_vita
  local out="${1:-}"
  local found=""
  # The log lives next to the config, in the first of these the app could
  # create (src/check_dir.c), with ux0:data as the last resort
  for path in "ux0:/data/moonlight/moonlight.log" "ux0:/moonlight/moonlight.log" "uma0:/data/moonlight/moonlight.log" "ux0:/data/moonlight.log"; do
    if curl -sf --max-time 10 "$(ftp_url "$path")" -o "${out:-/dev/stdout}"; then
      found="$path"
      break
    fi
  done
  [ -n "$found" ] || die "no moonlight.log found; is 'Enable debug log' on?"
  [ -z "$out" ] || echo "saved $found to $out"
}

cmd_crash() {
  require_vita
  local newest
  newest="$(curl -sf --max-time 10 "$(ftp_url "ux0:/data/")" | awk '/\.psp2dmp$/ {print $NF}' | sort | tail -n 1)"
  [ -n "$newest" ] || die "no core dumps in ux0:data"
  curl -sf --max-time 60 "$(ftp_url "ux0:/data/$newest")" -o "$REPO_ROOT/build/$newest"
  echo "saved build/$newest"
  echo "decode with: vita-parse-core build/$newest build/moonlight.elf"
}

case "${1:-}" in
  build) cmd_build ;;
  push) cmd_push ;;
  launch) cmd_launch ;;
  kill) cmd_kill ;;
  log) shift; cmd_log "${1:-}" ;;
  crash) cmd_crash ;;
  # One after the other, not joined with &&, which would turn off set -e inside
  # cmd_build and let a failed build upload the previous eboot.bin
  all) cmd_build; cmd_push ;;
  *) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
