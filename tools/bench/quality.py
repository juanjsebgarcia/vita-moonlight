"""
Score picture quality from Moonlight Bench frame grabs.

With bench_grab on, the Vita saves the decoded frame at fixed page ticks,
and run.sh fetches them as LABEL-ROUND-grab-TICK.raw. The page draws the same
picture at the same tick in every run, so grabs of one tick differ only by
what the stream did to them: compression, resolution and lost packets.

Each raw grab is converted to RGB, cropped to the part the Vita shows, and
scaled to the Vita's 960x544 screen, so runs at different resolutions are
compared as a player sees them. It's saved as a PNG to look at, then every
run is scored by PSNR against a reference run, over the visualiser only (the machine-readable
strip at the top and the HUD at the bottom left change between runs). The
reference is the run of the highest-bitrate variant at the largest
resolution, preferring variants that lost no frames, unless --ref names one.
Its other rounds give the noise floor: how close two runs of identical
settings get.

Usage: quality.py DIR [--ref LABEL]
"""

import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

SCREEN_WIDTH = 960
SCREEN_HEIGHT = 544

# The part of the frame scored, as fractions of its height: below the strip
# (LAYOUT.panelHeight in bench.html) and above the HUD
SCORED_TOP = 0.10
SCORED_BOTTOM = 0.86


def nv21_to_rgb(y_plane: np.ndarray, vu_plane: np.ndarray) -> np.ndarray:
    """
    Return the RGB image for an NV21 frame, using BT.601 limited range, which
    is what the client asks the host to encode.
    """
    height, width = y_plane.shape
    v = vu_plane[:, 0::2].astype(np.float64).repeat(2, axis=0).repeat(2, axis=1)[:height, :width] - 128
    u = vu_plane[:, 1::2].astype(np.float64).repeat(2, axis=0).repeat(2, axis=1)[:height, :width] - 128
    luma = (y_plane.astype(np.float64) - 16) * 1.164
    rgb = np.stack([luma + 1.596 * v, luma - 0.392 * u - 0.813 * v, luma + 2.017 * u], axis=-1)
    return np.clip(rgb + 0.5, 0, 255).astype(np.uint8)


def resize(image: np.ndarray, width: int, height: int) -> np.ndarray:
    """
    Return image scaled to width x height with bilinear filtering, like the
    Vita's GPU does when it draws the frame.
    """
    src_height, src_width, _ = image.shape
    if (src_width, src_height) == (width, height):
        return image
    ys = (np.arange(height) + 0.5) * src_height / height - 0.5
    xs = (np.arange(width) + 0.5) * src_width / width - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, src_height - 1)
    x0 = np.clip(np.floor(xs).astype(int), 0, src_width - 1)
    y1 = np.clip(y0 + 1, 0, src_height - 1)
    x1 = np.clip(x0 + 1, 0, src_width - 1)
    wy = np.clip(ys - y0, 0, 1)[:, None, None]
    wx = np.clip(xs - x0, 0, 1)[None, :, None]
    data = image.astype(np.float64)
    top = data[y0][:, x0] * (1 - wx) + data[y0][:, x1] * wx
    bottom = data[y1][:, x0] * (1 - wx) + data[y1][:, x1] * wx
    return np.clip(top * (1 - wy) + bottom * wy + 0.5, 0, 255).astype(np.uint8)


def read_grab(path: Path, grab: dict) -> np.ndarray:
    """
    Return a raw grab as the RGB picture the Vita shows, at screen size.
    """
    data = np.frombuffer(path.read_bytes(), dtype=np.uint8)
    stride = grab["stride"]
    height = grab["height"]
    if grab["format"] == "nv21":
        y_plane = data[: stride * height].reshape(height, stride)
        vu_plane = data[stride * height : stride * height + stride * (height // 2)].reshape(height // 2, stride)
        image = nv21_to_rgb(y_plane=y_plane, vu_plane=vu_plane)
    else:
        image = data[: stride * height].reshape(height, stride // 4, 4)[:, :, :3]
    x1, y1, x2, y2 = (int(round(v)) for v in grab["region"])
    return resize(image[y1:y2, x1:x2], width=SCREEN_WIDTH, height=SCREEN_HEIGHT)


def write_png(path: Path, image: np.ndarray) -> None:
    """
    Write an RGB array as an 8-bit PNG.
    """
    height, width, _ = image.shape
    rows = b"".join(b"\x00" + image[y].tobytes() for y in range(height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b"")
    )


def read_png(path: Path) -> np.ndarray:
    """
    Read a PNG written by write_png back into an RGB array.
    """
    data = path.read_bytes()
    offset = 8
    width = height = 0
    compressed = b""
    while offset < len(data):
        (length,) = struct.unpack(">I", data[offset : offset + 4])
        kind = data[offset + 4 : offset + 8]
        body = data[offset + 8 : offset + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            compressed += body
        offset += 12 + length
    raw = np.frombuffer(zlib.decompress(compressed), dtype=np.uint8)
    return raw.reshape(height, 1 + width * 3)[:, 1:].reshape(height, width, 3)


def psnr(image: np.ndarray, reference: np.ndarray) -> float:
    """
    Return the PSNR in dB of image against reference, over the scored rows.
    """
    height = image.shape[0]
    top = int(height * SCORED_TOP)
    bottom = int(height * SCORED_BOTTOM)
    diff = image[top:bottom].astype(np.float64) - reference[top:bottom].astype(np.float64)
    mse = float(np.mean(diff * diff))
    return 99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)


def load_runs(directory: Path) -> list[dict]:
    """
    Return each successful run in directory that has grabs, with its grabs as
    arrays keyed by tick. Raw grabs are converted to PNG and removed.
    """
    runs = []
    for report_path in sorted(directory.glob("*-*.json")):
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not report.get("ok") or not report.get("grabs"):
            continue
        stem = report_path.stem
        images = {}
        for grab in report["grabs"]:
            tick = grab["tick"]
            raw = directory / f"{stem}-grab-{tick}.raw"
            png = directory / f"{stem}-grab-{tick}.png"
            if raw.exists():
                write_png(png, read_grab(raw, grab))
                raw.unlink()
            if png.exists():
                images[tick] = read_png(png)
        if images:
            runs.append({"name": stem, "report": report, "images": images})
    return runs


def pick_reference(runs: list[dict], label: str | None) -> str:
    """
    Return the label of the variant to score against.
    """
    if label:
        return label

    def rank(run: dict) -> tuple:
        settings = run["report"].get("settings", {})
        frames = run["report"].get("frames", {})
        clean = frames.get("lost_network", 1) == 0
        return (settings.get("width", 0), clean, settings.get("bitrate_kbps", 0))

    return max(runs, key=rank)["report"]["label"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--ref", help="variant label to score against")
    args = parser.parse_args()

    runs = load_runs(args.directory)
    if not runs:
        print("No frame grabs found.", file=sys.stderr)
        return 1

    ref_label = pick_reference(runs, args.ref)
    ref_runs = [r for r in runs if r["report"]["label"] == ref_label]
    if not ref_runs:
        print(f"No grabs for reference variant {ref_label}.", file=sys.stderr)
        return 1
    reference = ref_runs[0]

    scores: dict[str, list[float]] = {}
    for run in runs:
        if run is reference:
            continue
        common = sorted(set(run["images"]) & set(reference["images"]))
        if not common:
            continue
        values = [psnr(run["images"][tick], reference["images"][tick]) for tick in common]
        scores.setdefault(run["report"]["label"], []).extend(values)

    print(f"PSNR against {reference['name']} (higher is closer; 40+ dB looks identical, under 30 shows artefacts)")
    print()
    print("| variant | grabs | PSNR mean dB | PSNR worst dB |")
    print("|---|---|---|---|")
    for label, values in scores.items():
        note = " (noise floor)" if label == ref_label else ""
        mean = sum(values) / len(values)
        print(f"| {label}{note} | {len(values)} | {mean:.1f} | {min(values):.1f} |")
    print()
    print(f"PNGs of every grab are in {args.directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
