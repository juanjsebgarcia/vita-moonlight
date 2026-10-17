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

set -euo pipefail

[ $# -eq 1 ] || { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[ -n "${VITA_IP:-}" ] || { echo "error: set VITA_IP to the Vita's address" >&2; exit 1; }
out="$1"
dir="ux0:/data/moonlight"
ftp="ftp://$VITA_IP:${VITA_FTP_PORT:-1337}"
raw="$(mktemp)"
trap 'rm -f "$raw"' EXIT

# Remove the previous capture, so only a new one is fetched
curl -s --max-time 10 -Q "DELE $dir/shot.raw" "$ftp/" -o /dev/null 2>/dev/null || true
printf 'shot\n' | curl -sSf --max-time 10 -T - "$ftp/$dir/shot.req"

for _ in $(seq 1 20); do
  sleep 0.5
  if curl -sf --max-time 10 "$ftp/$dir/shot.raw" -o "$raw" && [ "$(wc -c < "$raw")" -eq $((960 * 544 * 4)) ]; then
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
