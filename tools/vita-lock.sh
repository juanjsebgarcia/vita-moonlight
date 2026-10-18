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
# for 20 minutes is treated as abandoned and broken; holders refresh it every
# minute. It's released when the command ends, or on Ctrl-C. Killing only
# this script releases it once the command has ended (bash waits for it),
# so the lock is never released while the command still drives the Vita;
# to stop both, press Ctrl-C or kill the process group. Nested calls (a
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
# Without trailing slashes, so the guard ($LOCK.guard) sits beside the lock,
# not inside it
while [[ "$LOCK" == */ && "$LOCK" != / ]]; do LOCK="${LOCK%/}"; done
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
# The owner is only a name to show, and may not be unique; this identifies
# this one taking of the lock
token="$$.$RANDOM.$(date +%s)"
mkdir -p "$(dirname "$LOCK")"

# A short-lived second lock around everything that changes or removes a lock
# that's already taken (breaking an abandoned one, the holder's refresh, and
# release), so a waiter can't remove a lock someone has just taken fresh.
GUARD="$LOCK.guard"
take_guard() {
  local i
  for (( i = 0; i < 50; i++ )); do
    mkdir "$GUARD" 2>/dev/null && return 0
    # It's only ever held for a few milliseconds, so one over a minute old
    # was left by a process killed while holding it. Renamed away before it's
    # removed, so of several waiters that see it, only one removes it.
    if [ -n "$(find "$GUARD" -maxdepth 0 -mmin +1 2>/dev/null)" ] \
        && mv "$GUARD" "$GUARD.stale.$$" 2>/dev/null; then
      rm -rf "$GUARD.stale.$$"
      continue
    fi
    sleep 0.1
  done
  return 1
}
drop_guard() {
  rmdir "$GUARD" 2>/dev/null
}
ours() {
  [ "$(cat "$LOCK/token" 2>/dev/null)" = "$token" ]
}

# Succeeds, printing whose it was, when the lock is abandoned: its holder
# hasn't been seen for STALE_SECONDS, or it was taken over a minute ago and
# never given an owner (whoever took it died)
abandoned() {
  local who seen
  if read -r who seen 2>/dev/null < "$LOCK/owner" && [[ "$seen" =~ ^[0-9]+$ ]]; then
    [ $(( $(date +%s) - seen )) -gt "$STALE_SECONDS" ] && echo "held by $who"
    return
  fi
  [ -n "$(find "$LOCK" -maxdepth 0 -mmin +1 2>/dev/null)" ] && echo "with no owner"
}

release() {
  if ! take_guard; then
    # Removing it unguarded could remove someone else's; left as it is, it
    # counts as abandoned once the heartbeat has stopped
    echo "warning: couldn't release the Vita lock; others can take it in $(( STALE_SECONDS / 60 )) minutes" >&2
    return
  fi
  # Only if it's still ours: it may have been broken as abandoned
  ours && rm -rf "$LOCK"
  drop_guard
}

announced=""
for (( waited = 0; waited < WAIT_SECONDS; waited++ )); do
  if mkdir "$LOCK" 2>/dev/null; then
    now="$(date +%s)"
    if ! { echo "$token" > "$LOCK/token" && echo "$now" > "$LOCK/started" \
        && echo "$owner $now" > "$LOCK/owner.tmp" && mv "$LOCK/owner.tmp" "$LOCK/owner"; }; then
      # Without them, others could take it for abandoned while the command
      # runs. Just taken, so nobody else can have broken it yet.
      echo "error: couldn't record the Vita lock in $LOCK" >&2
      rm -rf "$LOCK"
      exit 1
    fi
    trap release EXIT
    # Trapped, so bash waits for the command before releasing the lock
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    # Shows the holder is alive, so a long benchmark isn't taken for an
    # abandoned lock. It's detached, so this script has no child to stop or
    # reap; it ends by itself within a second once this script has exited
    # or the lock is no longer ours (released, or broken as abandoned after,
    # say, the Mac slept for longer than STALE_SECONDS).
    (
      (
        for (( beat = 1; ; beat++ )); do
          sleep 1
          kill -0 $$ 2>/dev/null && ours || exit 0
          (( beat % 60 == 0 )) || continue
          take_guard || continue
          ours && echo "$owner $(date +%s)" > "$LOCK/owner.tmp" && mv "$LOCK/owner.tmp" "$LOCK/owner"
          drop_guard
        done
      ) </dev/null >/dev/null 2>&1 &
    )
    export VITA_LOCK_HELD="$owner"
    "$@"
    exit $?
  fi
  if [ -n "$(abandoned)" ] && take_guard; then
    # Checked again holding the guard, in case another waiter broke it and
    # took it fresh in the meantime
    why="$(abandoned)"
    if [ -n "$why" ]; then
      echo "breaking the abandoned Vita lock $why" >&2
      rm -rf "$LOCK"
    fi
    drop_guard
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
