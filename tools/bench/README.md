# Moonlight Bench

`bench.html` is a fixed, reproducible target for measuring streaming performance. Run it full screen on the Sunshine host, stream it to the Vita, and every run sees the same pictures at the same moments. That keeps comparisons between builds or settings fair.

It's one self-contained HTML file with no dependencies, so it works the same on Windows, macOS and Linux in any current browser.

## What's on screen

- **The visualiser.** A Winamp/MilkDrop-style mix of plasma, a checkered tunnel, particles, a spirograph and spinning polygons. In `stress` mode nearly every pixel changes every frame, the worst case for the host's video encoder, which pushes bitrate, packet count, FEC and decode as hard as they go. `typical` mode is calmer, closer to a real game.
- **The machine-readable strip.** A black band across the top, holding:
  - an **input marker**
  - a **frame counter barcode**

  Its layout is described in [Layout contract](#layout-contract).
- **A small HUD,** bottom left, showing the mode, resolution, time, tick, drawn frames, browser fps and input count. Press H to hide it.

**Reproducibility:** everything is drawn from the time since the last reset and a fixed seed, so input never changes the scene. Only the marker reacts.

## Controls

| Input | Effect |
|---|---|
| R | Reset: time and the tick counter go back to zero |
| H | Show or hide the HUD |
| F | Toggle browser fullscreen |
| Any other key, a gamepad button, a click or a touch | Lights the input marker for 100 ms |

## URL parameters

| Parameter | Values | Default | Meaning |
|---|---|---|---|
| `mode` | `stress`, `typical` | `stress` | How much motion and detail |
| `fps` | a number | `60` | Ticks per second for the barcode. Set it to the stream's frame rate. |

For example, `bench.html?mode=typical&fps=30`.

## Setting it up as a Sunshine app

Add an application in Sunshine's web UI (**Applications → Add New**). Name it `Moonlight Bench`, and use one of these as its **Command**. Each opens the page in a separate browser instance in kiosk mode: full screen, no browser UI.

The separate `--user-data-dir` matters. Without it, if the browser is already running, the new window joins the existing instance and the kiosk flags are ignored.

**Windows (Microsoft Edge)**

```
"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --kiosk "file:///C:/path/to/vita-moonlight/tools/bench/bench.html?mode=stress" --edge-kiosk-type=fullscreen --no-first-run --user-data-dir=C:\Temp\moonlight-bench
```

**Windows (Google Chrome)**

```
"C:\Program Files\Google\Chrome\Application\chrome.exe" --kiosk "file:///C:/path/to/vita-moonlight/tools/bench/bench.html?mode=stress" --no-first-run --user-data-dir=C:\Temp\moonlight-bench
```

**macOS (Google Chrome)**

```
open -na "Google Chrome" --args --kiosk "file:///Users/you/vita-moonlight/tools/bench/bench.html?mode=stress" --no-first-run --user-data-dir=/tmp/moonlight-bench
```

**Linux (Chromium)**

```
chromium --kiosk "file:///home/you/vita-moonlight/tools/bench/bench.html?mode=stress" --no-first-run --user-data-dir=/tmp/moonlight-bench
```

On some distributions the binary is `chromium-browser` or `google-chrome`.

Tips for consistent results:

- **Refresh rate:** run the host display at 60 Hz if you can. The barcode already counts stream-rate ticks rather than monitor refreshes, but a 60 Hz desktop matches what Sunshine captures.
- **Keep the page in front.** Background tabs are throttled, so the page must stay focused and visible.
- **Compare like with like:** use the same `mode` for runs you compare.

## Checking it by hand

1. Stream "Moonlight Bench" to the Vita.
2. Press any button. The top-left marker should flash white.
3. With the stats overlay on `full`, the Vita's figures now reflect a known, repeatable load.

## Layout contract

The strip is read by the Vita client from decoded video. All values are fractions of the viewport (`x` and widths of the width, `y` and heights of the height), so they hold at any capture resolution or scale. **Change these only together with the Vita reader.**

| Element | Position | Meaning |
|---|---|---|
| Strip | `y` 0 to 0.10, full width | Always black apart from the elements below; the visualiser is drawn under it |
| Input marker | `x` 0.010, `y` 0.015, `w` 0.060, `h` 0.070 | White for 100 ms after any input, otherwise black |
| Barcode | starts at `x` 0.100, `y` 0.020, `h` 0.060; each block `w` 0.045 | Block 0 is always white, block 1 always black, then 16 data blocks |

The barcode's 16 data blocks hold the tick count modulo 65536, most significant bit first, with white meaning 1. The tick count is `floor(ms since reset × fps / 1000)`.

### Reading it

To decode the strip from a frame:

1. Sample the luminance at the centre of each block, at `y = 0.020 + 0.060 / 2`.
2. Set a threshold halfway between the white reference (block 0) and the black reference (block 1).
3. Read data block `i` (0 to 15) at `x = 0.100 + 0.045 × (i + 2.5)`. It is 1 if its luminance is above the threshold.
4. Read the marker at its centre, using the same threshold.

What to look for in the readings:

- **Consecutive streamed frames** should differ by exactly one tick:
  - **a step of 0:** a repeated frame
  - **a step of 2 or more:** frames lost, on the host or on the network
  - Compare with the network's lost-frame count to tell which.
- **Input latency** is the time from the client sending a key press to the first decoded frame where the marker is white. Both times are on the client's clock, so no clock sync is needed. It covers the whole loop: client to host, the browser drawing, capture, encode, the network back, and decode. The display's own scan-out is the only part it misses.

The block sizes, at least 0.045 of the width by 0.06 of the height (about 43x32 pixels at 960x540), survive H.264 compression and chroma subsampling comfortably.

## Status

The page is done. The Vita side, a hidden developer mode that runs a benchmark against this page and writes a report, is in progress. See `docs/DEVELOPMENT.md` for how builds are deployed to a Vita for testing.
