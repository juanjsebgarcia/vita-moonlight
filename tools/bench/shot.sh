#!/usr/bin/env bash
# Take a screenshot of Moonlight's menus on a Vita, as a PNG.
#
# Usage: tools/bench/shot.sh OUT.png
#
# Needs VITA_IP, vitacompanion, and developer mode on in Moonlight
# (developer_mode = true in moonlight.conf). The app saves the next frame
# it draws when it finds a request file (src/devshot.c). Combine with
# vitacompanion's synthetic input to drive the menus, e.g.
#   printf 'press down; wait 100ms; release down\n' | nc -w 3 "$VITA_IP" 1338
#
# It takes the Vita lock (tools/vita-lock.sh) unless a parent script already
# holds it; wrap a batch of shots and presses in the lock yourself, so they
# aren't interleaved with someone else's. VITA_LOCK=off skips it.

set -euo pipefail

[ $# -eq 1 ] || { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[ -n "${VITA_IP:-}" ] || { echo "error: set VITA_IP to the Vita's address" >&2; exit 1; }
if [ -z "${VITA_LOCK_HELD:-}" ] && [ "${VITA_LOCK:-}" != off ]; then
  here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  exec bash "$here/../vita-lock.sh" bash "$here/shot.sh" "$@"
fi
out="$1"
dir="ux0:/data/moonlight"
ftp="ftp://$VITA_IP:${VITA_FTP_PORT:-1337}"
raw="$(mktemp)"
trap 'rm -f "$raw"' EXIT

size=$((960 * 544 * 4))

# vitacompanion's FTP server gives up under heavy polling (transfers hang at
# 0 bytes), so this keeps to few, cheap sessions: one to remove the previous
# capture and ask for a new one, a directory listing a second to see when the
# capture is there, and one download of the finished file.
# The leading * lets the upload go ahead when there's no capture to remove.
printf 'shot\n' | curl -sSf --max-time 10 -Q "*DELE $dir/shot.raw" -T - "$ftp/$dir/shot.req"

for _ in $(seq 1 10); do
  sleep 1
  # The app writes shot.raw.tmp and renames it, so shot.raw at full size is complete
  listed="$(curl -sf --max-time 10 "$ftp/$dir/" | tr -d '\r' | awk '$NF == "shot.raw" { print $5 }')" || listed=""
  [ "$listed" = "$size" ] || continue
  if curl -sf --max-time 20 "$ftp/$dir/shot.raw" -o "$raw" && [ "$(wc -c < "$raw")" -eq "$size" ]; then
    python3 -I - "$raw" "$out" <<'PY'
import struct, sys, zlib

data = open(sys.argv[1], "rb").read()
rows = b"".join(b"\x00" + bytes(c for i in range(y * 960 * 4, (y + 1) * 960 * 4, 4) for c in data[i:i + 3]) for y in range(544))

def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

with open(sys.argv[2], "wb") as f:
    f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 960, 544, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))
PY
    echo "saved $out"
    exit 0
  fi
done
echo "error: no screenshot after 10 s. Is Moonlight showing a menu, with developer mode on?" >&2
exit 1
