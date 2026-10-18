#!/usr/bin/env bash
# The Vita lock: lets only one session drive a PS Vita at a time.
#
# Usage: tools/vita-lock.sh COMMAND [ARGS...]   run the command holding the lock
#        tools/vita-lock.sh --status            print who holds it, and for how long
#
# tools/vita.sh (everything but build), tools/bench/run.sh and
# tools/bench/shot.sh take the lock themselves, so you only need this to wrap
# other things that talk to the Vita: vitacompanion button presses (port 1338)
# or FTP reads and writes (port 1337). Hold it for one short batch, then let
# it go so others get a turn.
#
# It waits up to 30 minutes for the lock. A lock whose holder hasn't been seen
# for 20 minutes is treated as abandoned and broken. It's released when the
# command ends, on Ctrl-C, or when this script is killed. Nested calls (a
# script that takes the lock running another that does) don't wait: the
# holder exports VITA_LOCK_HELD, and with it set this just runs the command.
#
# Environment:
#   VITA_LOCK_OWNER  name shown to others waiting (default user@host:pid)
#   VITA_LOCK        lock directory, shared by every worktree of the clone
#                    (default: vita.lock in the clone's .git directory), or
#                    "off" to turn locking off when you're the only user
#   VITA_LOCK_HELD   set by the holder; don't set it yourself

set -uo pipefail

usage() {
  awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
  exit 2
}

# The default is in the git common directory, so every worktree of the clone
# shares one lock. Outside a clone, fall back to the temporary directory.
default_lock() {
  local here common
  here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  if common="$(cd "$here" && git rev-parse --git-common-dir 2>/dev/null)" \
      && common="$(cd "$here" && cd "$common" && pwd)"; then
    echo "$common/vita.lock"
  else
    echo "${TMPDIR:-/tmp}/vita-moonlight.lock"
  fi
}

LOCK="${VITA_LOCK:-$(default_lock)}"
STALE_SECONDS=1200
WAIT_SECONDS=1800

# Prints "OWNER SECONDS_HELD", or nothing when the lock is free. The owner
# file holds "OWNER LAST_SEEN" (the holder refreshes it every minute), and
# started holds when the lock was taken.
holder() {
  local who seen started
  read -r who seen 2>/dev/null < "$LOCK/owner" || return 0
  started="$(cat "$LOCK/started" 2>/dev/null)"
  [[ "$started" =~ ^[0-9]+$ ]] || started="$seen"
  [[ "$started" =~ ^[0-9]+$ ]] || started="$(date +%s)"
  echo "$who $(( $(date +%s) - started ))"
}

case "${1:-}" in
  --status)
    h=""
    [ "$LOCK" = off ] || h="$(holder)"
    if [ "$LOCK" = off ]; then
      echo "off (VITA_LOCK=off)"
    elif [ -n "$h" ]; then
      echo "held by ${h% *} for ${h##* }s"
    else
      echo "free"
    fi
    exit 0
    ;;
  ""|-h|--help) usage ;;
esac

# Turned off, or a parent process already holds it: just run
if [ "$LOCK" = off ] || [ -n "${VITA_LOCK_HELD:-}" ]; then
  exec "$@"
fi

owner="${VITA_LOCK_OWNER:-${USER:-$(id -un)}@$(hostname -s 2>/dev/null || hostname):$$}"
owner="${owner//[[:space:]]/_}"
mkdir -p "$(dirname "$LOCK")"

release() {
  local who seen
  [ -n "${heartbeat:-}" ] && kill "$heartbeat" 2>/dev/null
  # Only remove the lock if it's still ours: it may have been broken as abandoned
  read -r who seen 2>/dev/null < "$LOCK/owner"
  [ "${who:-}" = "$owner" ] && rm -rf "$LOCK"
}

announced=""
for (( waited = 0; waited < WAIT_SECONDS; waited++ )); do
  if mkdir "$LOCK" 2>/dev/null; then
    now="$(date +%s)"
    echo "$now" > "$LOCK/started"
    echo "$owner $now" > "$LOCK/owner.tmp" && mv "$LOCK/owner.tmp" "$LOCK/owner"
    trap release EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    # Shows the holder is alive, so a long benchmark isn't taken for an
    # abandoned lock. It stops by itself if this script is killed outright.
    (
      while sleep 60 && kill -0 $$ 2>/dev/null; do
        echo "$owner $(date +%s)" > "$LOCK/owner.tmp" && mv "$LOCK/owner.tmp" "$LOCK/owner"
      done
    ) </dev/null >/dev/null 2>&1 &
    heartbeat=$!
    export VITA_LOCK_HELD="$owner"
    "$@"
    exit $?
  fi
  if read -r who seen 2>/dev/null < "$LOCK/owner" && [[ "$seen" =~ ^[0-9]+$ ]]; then
    if [ $(( $(date +%s) - seen )) -gt "$STALE_SECONDS" ]; then
      echo "breaking the abandoned Vita lock held by $who" >&2
      rm -rf "$LOCK"
      continue
    fi
  elif [ -n "$(find "$LOCK" -maxdepth 0 -mmin +1 2>/dev/null)" ]; then
    # Taken over a minute ago but never given an owner: whoever took it died
    echo "breaking a Vita lock that has no owner" >&2
    rm -rf "$LOCK"
    continue
  fi
  if [ -z "$announced" ]; then
    echo "waiting for the Vita lock ($(bash "$0" --status))" >&2
    announced=1
  fi
  sleep 1
done
echo "timed out waiting for the Vita lock ($(bash "$0" --status))" >&2
exit 1
