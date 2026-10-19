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
- **Test audio, off until you press M.** Browsers won't play sound until a key is pressed, so it isn't on by default. It's built to be hard to compress, so the stream carries a full audio track: seeded stereo noise, a detuned sawtooth chord under a sweeping filter, and an FM voice, mixed differently in each ear. It's the same every run. Without it the page is silent, and Sunshine sends no audio packets at all while nothing on the computer plays sound.

**Reproducibility:** everything is drawn from the tick since the last reset and a fixed seed, so a given tick always shows the same picture, and input never changes the scene. Only the marker reacts. That's what lets frame grabs from different runs be compared picture for picture.

## Controls

| Input | Effect |
|---|---|
| R | Reset: time and the tick counter go back to zero, and the test audio (if on) restarts from its beginning |
| H | Show or hide the HUD |
| F | Toggle browser fullscreen |
| M | Test audio on or off (Shift+M only turns it on, Ctrl+M only off) |
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
| Cursor lane | `x` 0.915, `y` 0.010, `w` 0.080, `h` 0.080 | Always black, with a faint outline, so the mouse pointer stands out |
| Cursor targets | hotspots at (0.930, 0.020) and (0.965, 0.020) | Where cursor mode puts the pointer. The pointer's arrow is looked for in a box `w` 0.012, `h` 0.035 below and right of each hotspot. |

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

In cursor mode, the pointer counts as at a target when the brightest pixel in that target's box is above the reference threshold. It needs Windows' standard (white) pointer, at any size; a black or inverted pointer scheme won't be seen. If **Hide pointer while typing** is on in Windows' mouse settings, it doesn't matter, because cursor mode sends no keys after the reset.

The block sizes, at least 0.045 of the width by 0.06 of the height (about 43x32 pixels at 960x540), survive H.264 compression and chroma subsampling comfortably.

## Running a benchmark on the Vita

The Vita side lives behind a hidden **developer mode**, so it stays out of normal players' way.

### From the Vita

1. In **Settings**, hold **L** and **R** and press **△**. A message confirms that developer options are shown. They appear in a **Developer** section at the end of Settings. The same combination, or **Hide developer options**, hides them again.
2. Connect to the computer as usual, so you're on its menu with the app list, then open **Settings** from that menu. The benchmark runs against whichever computer you're connected to.
3. Under **Developer**, **Run benchmark** shows what it will use, for example "Moonlight Bench on DESKTOP-RYZEN".
   - **Finding the app:** it looks in the computer's app list for an exact, case-insensitive match on `bench_app` ("Moonlight Bench" by default), then for any app with "bench" in its name.
   - **If there isn't one,** it says so instead of streaming.

   **Benchmark duration** sets how long it measures (30, 60, 120 or 300 s).
4. Press ✕ on **Run benchmark**. The Vita:
   1. starts the app and waits up to 45 s for the page to appear (it checks the reference blocks)
   2. presses Shift+M (with `bench_audio = true`) or Ctrl+M, so the test audio is on or off whatever an earlier run left, then R to reset the page
   3. measures for the set duration, pressing Space about once a second to time input lag
   4. ends the stream, quits the app on the computer, and shows a summary

**Reports** go to `ux0:data/moonlight/reports/` as `bench-<date>-<time>.json`, with a copy of the latest as `last-bench.json`.

### Unattended, comparing settings

`tools/bench/run.sh` runs the benchmark with no one at the Vita, and compares variants:

```sh
export VITA_IP=192.168.1.50
tools/bench/run.sh base legacy:direct_decode=true big:packetsize=1392
tools/bench/run.sh --seconds 120 --rounds 3 --host DESKTOP-RYZEN base pacer:enable_frame_pacer=true
```

Each variant is `LABEL`, or `LABEL:key=value,key=value` where the keys are `moonlight.conf` settings. A plain `LABEL` uses your current settings.

For each run, the script:
1. **Writes the variant's settings.** It writes `moonlight.conf` with the variant's keys plus `bench_autorun = true`.
2. **Runs it.** It launches Moonlight, which runs the benchmark straight away against `--host` (or the first paired computer), writes the report and exits.
3. **Saves the report** into `build/bench/<time>/`.

Every run turns the stats overlay and debug log off, since both cost time themselves, unless the variant sets `stats_overlay` or `save_debug_log`.

Variants run in turn, round after round, so changing Wi-Fi conditions affect them all alike. At the end, `compare.py` prints a table (also saved as `summary.md`) with the mean for each variant and the min-max spread across rounds. **Your original `moonlight.conf` is backed up first and restored at the end, even if a run fails.**

It needs `tools/vita.sh` and the Vita setup in `docs/DEVELOPMENT.md`, with the computer awake and Sunshine running.

#### Settings worth comparing

| Key | Values | What it changes |
|---|---|---|
| `bitrate` | kbps, e.g. `5000`, `10000`, `15000` | Video bitrate |
| `width`, `height` | e.g. `960`/`544`, `1280`/`720` | Stream resolution (the host must support it) |
| `fps` | `30`, `60` | Stream frame rate |
| `packetsize` | `1024`, `1392` | Video packet size |
| `direct_decode` | `true`, `false` | Legacy decode on the network thread |
| `input_sync_to_controller` | `true`, `false` | Input reads paced to the controller, or 2 ms polling |
| `boost_clocks` | `true`, `false` | CPU, bus and GPU clocks raised while streaming |
| `enable_frame_pacer` | `true`, `false` | The frame pacer |
| `enable_vita_vblank_wait` | `true`, `false` | V-Sync |
| `stats_overlay` | `0`, `1`, `2` | Overlay off, fps or full (its drawing cost counts in the results; off by default in `run.sh`) |
| `bench_input` | `key`, `mouse`, `gamepad`, `cursor` | What the timed presses send. `key`, `mouse` and `gamepad` send Space, a left click or gamepad A, and wait for the page to light its marker. Each takes a different route through the host. `cursor` instead moves the mouse pointer between two targets, which Windows draws and Sunshine captures, so it times the host's input and capture **without the browser**. Compare it with `key` to see how much of the delay is the browser: `tools/bench/run.sh key:bench_input=key cursor:bench_input=cursor` |

To compare **builds** rather than settings, deploy each build with `tools/vita.sh push` and run the same variant against it, with a different label.

### Report contents

| Field | Meaning |
|---|---|
| `ok`, `error` | Whether the run completed, and why not |
| `label`, `started`, `host`, `app`, `seconds`, `version` | What ran, where and for how long |
| `settings` | The settings that matter for performance |
| `audio` | Audio packets that reached the decoder during the measurement, and per second (about 200 with sound playing; 0 means the computer sent none) |
| `frames` | Frames received and drawn, fps, frames lost on the network, IDR frames |
| `page` | From the barcode: ticks read, unreadable frames, `repeated` (the same picture twice), `skipped` (ticks the Vita never saw), the measured `tick_rate`, `expected_skips_from_rate`, and `skipped_beyond_rate` |
| `input_lag_ms` | Presses sent, seen and missed; p50, p95, min and max from sending a key to the first frame on screen with the marker lit |
| `latency_estimate_ms` | Host time + receive + queue + decode + render per frame, plus half the round trip (see the stats overlay) |
| `network_ms`, `queue_ms`, `decode_ms`, `render_ms` | p50, p95 and max for each stage |
| `bitrate_mbps`, `rtt_ms`, `wifi` | Measured bitrate, round trip, and Wi-Fi signal and channel |

**Skipped ticks need the rate taken into account.** The page ticks at its own rate (60 per second unless `?fps=` says otherwise). So a stream slower than that skips ticks with nothing lost: a 50 fps stream skips about 10 a second. `expected_skips_from_rate` is that gap, and `skipped_beyond_rate` is what's left, meaning pictures lost on the network or dropped by the host. Compare it with `frames.lost_network` to tell which.

**Input lag is the most direct measure**: it's timed entirely on the Vita's clock and covers the whole loop. Its precision is one frame, since the marker shows up in whichever frame the page draws next.

### Settings used by the benchmark

These live in `moonlight.conf`, and the menu sets the first two:

| Key | Default | Meaning |
|---|---|---|
| `developer_mode` | `false` | Show the Developer section |
| `bench_seconds` | `60` | Seconds to measure (10 to 600) |
| `bench_app` | `Moonlight Bench` | Preferred benchmark app name |
| `bench_host` | empty | Unattended runs only: the paired computer to use (empty means the first paired one) |
| `bench_label` | empty | Copied into the report, e.g. a variant name |
| `bench_autorun` | `false` | Run once at launch and exit; cleared before the run starts |
| `bench_quit_app` | `true` | Quit the app on the computer when the run ends |
| `bench_input` | `key` | What the timed presses send: `key`, `mouse`, `gamepad` or `cursor` |
| `bench_audio` | `false` | Turn the page's test audio on (Shift+M; Ctrl+M when false) before the run, so audio is part of what's measured. Ignored with `bench_input = cursor` or `none`, which may not be showing the page; `settings.page_audio` in the report says whether it was turned on |
