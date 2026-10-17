# Latency and decode: findings and goals

This is the plan for making streams to the Vita feel as responsive as the hardware allows. It records what has been measured, where the time goes, and what to try next. Each experiment says how to measure it, so results can be compared and recorded here.

Measurements come from the Moonlight Bench tools; see [`tools/bench/README.md`](../tools/bench/README.md) and [`docs/DEVELOPMENT.md`](DEVELOPMENT.md). Unless noted, the figures below are p50 (typical) values.

## Where the time goes today

Measured on 2026-10-16:
- **Host:** Windows with Sunshine on an Intel Arc B580, so Quick Sync encode; capture via DDX; 60 Hz desktop.
- **Network:** 2.4 GHz Wi-Fi with the Vita next to the access point.
- **Stream:** 960x540 at 60 fps, 10 Mbps.

### Input to screen, pointer path (no browser): about 73 ms

| Stage | Time | Source |
|---|---|---|
| Vita: input call → packet on the network | **0.18 ms** (p95 0.23) | `send_ms` in the bench report (ENet calls wrapped at link time) |
| Network, both ways | ~3.5 ms | ENet RTT, measured continuously |
| PC: input applied → frame captured → encoded → sent | **~60 ms (3 to 4 frames)** | What remains of the measured total |
| Of which, encode | ~1.7 ms | Host processing latency reported per frame by Sunshine |
| Vita: receive, queue, decode, draw | ~8 ms | Per-frame timing, below |

### Vita side, per frame

| Stage | Time | What it is |
|---|---|---|
| Receive (`network_ms`) | 2.5 to 4 ms | First packet to last packet of a frame; bound by Wi-Fi and frame size |
| Queue (`queue_ms`) | ~0.1 ms | Waiting for the decode thread |
| Decode (`decode_ms`) | **~4.7 ms** (p95 5.4) | Assembling the frame (~20 µs), then `sceAvcdecDecode` |
| Draw (`render_ms`) | ~0.4 ms | Waiting for the previous draw, drawing and swapping; ~1.4 ms with the stats overlay on |
| Display scan-out | **not measured** | Up to one refresh (~17 ms) after the swap; needs a camera |

### What has been ruled out, and how

- **The network.** With the Vita next to the access point, the round trip halved to ~4 ms and frame loss went to zero, but input lag didn't change.
- **The Vita's send path.** 0.18 ms from the input call to the datagram leaving, measured directly.
- **The encoder.** Sunshine reports ~1.7 ms from capture to an encoded frame.
- **Sunshine's capture method.** WGC and DDX gave the same input lag (185.5 vs 188.8 ms on the browser test).
- **The input type.** Key, mouse and gamepad all gave 185 to 206 ms on the browser test.

### Other findings

- **Frame rate mismatch costs ~30 ms.** A 50 fps stream from a 60 Hz desktop was ~30 ms slower than 60 fps.
- **The browser adds ~80 to 100 ms.** The page's marker test, 175 to 205 ms, includes the browser drawing it. The pointer test doesn't. Use cursor mode for input-lag numbers.
- **The PC's latency varies run to run,** from 73 to 125 ms on the pointer test. It once stepped up by ~35 ms partway through a run with nothing changing on the Vita, which suggests power states on the PC, even with Windows on High Performance.

## Goals

1. **Keep the Vita side under 10 ms** from a frame's first packet to the swap, and keep it measured on every change.
2. **Cut the decode stage** (currently ~4.7 ms) where the hardware allows, and know exactly how it splits.
3. **Measure everything that's currently guessed:**
   - display scan-out
   - the audio-to-video offset
   - a glass-to-glass figure, to validate the instrumentation
4. **Make the default settings the low-latency ones** where there's no quality cost, and explain the trade-offs in the settings help.
5. **Keep the benchmark trustworthy:** reproducible, cheap to run, and documented.

The PC side (~60 ms) belongs to Sunshine, Windows and the GPU driver, and is outside this project. It's worth documenting good host settings, but it isn't a goal here.

## Experiments, in order

Each one says how to measure it. Add results to [Results](#results).

| # | Experiment | Hypothesis | How to measure | Effort |
|---|---|---|---|---|
| 1 | **Thread priority and core affinity** | If the decode, receive, audio and input threads share one core, they queue behind each other | One run with `save_debug_log=true`, then read the `[THREAD]` lines in `moonlight.log` | None |
| 2 | **Split the decode timing** into frame assembly and `sceAvcdecDecode` | Nearly all of the 4.7 ms is the hardware decoder | Add a timer around `sceAvcdecDecode`, report it as its own stage | Tiny |
| 3 | **Bitrate and packet size** | Fewer bits per frame shorten receive, and possibly decode | `tools/bench/run.sh --app Desktop b5:bench_input=cursor,bitrate=5000 b10:bench_input=cursor,bitrate=10000 p1392:bench_input=cursor,packetsize=1392` | No code |
| 4 | **Decode to YUV, convert on the GPU** | If colour conversion is part of the hardware decode, outputting YUV420 and sampling it with GXM's built-in CSC formats saves 1 to 2 ms | Prototype behind a developer setting, then compare `decode_ms` and the pointer lag | Moderate |
| 5 | **Bump moonlight-common-c** (40 commits behind) | The NEON FEC path lowers CPU cost when packets are lost; also brings leak, RTSP and frame-recovery fixes | Build check with VitaSDK, then compare with loss present (e.g. far from the AP) | Low to medium |
| 6 | **Audio to video offset** | Audio trails video by a few tens of ms (20 ms output blocks, blocking output) | The page plays a click with each marker flash; the Vita times decoded audio against the marker | Moderate |
| 7 | **Double-buffer audio output** (first sweep, item 7) | Removes possible crackle, and shortens audio latency | Item 6's measurement before and after | Small |
| 8 | **Glass-to-glass** | Confirms the instrumentation, and measures scan-out | Film the Vita screen and a press in slow motion (240 fps) | A phone |
| 9 | **Native benchmark target** (SDL2, built by CI for Windows, macOS and Linux) | Removes the browser's ~80 to 100 ms from the marker test, making it reflect games | Compare its marker lag with cursor mode | Moderate |
| 10 | **Skip the screen clear when the video covers it** | ~0.1 ms of GPU time | `render_ms` before and after | Tiny |

Not worth doing for now:
- **Decoding straight into the display buffer,** to skip the GPU draw. It saves ~0.4 ms and complicates overlays.
- **V-Sync on.** It adds up to a frame; keep it off by default.

## Settings that matter today

| Setting | Recommendation | Why |
|---|---|---|
| FPS | Match the host desktop's refresh rate (60 on a 60 Hz desktop) | A 50/60 mismatch measured ~30 ms slower |
| Frame pacer | Off | It drops decoded frames periodically |
| V-Sync (Enable VITA vblank) | Off | It adds up to a frame of latency |
| Stats overlay | Off when not diagnosing | It costs ~1 ms of draw time per frame |
| Clock boost | On | Faster depacketising, FEC, audio and draw |
| Vita placement | Strong 2.4 GHz signal, Bluetooth off if not needed | Cut frame loss from ~50/min to 0 in testing |

## Results

Add a row per experiment: date, build or branch, host setup, command, and what changed.

| Date | Experiment | Setup | Result |
|---|---|---|---|
| 2026-10-16 | Send path (cursor, Desktop) | B580, DDX, 60 fps, next to AP | To wire 0.18 ms p50; pointer lag 73 ms p50, 4 to 5 frames |
| 2026-10-16 | Frame rate | Browser marker, key input | 60 fps 175.8 ms vs 50 fps 205.5 ms |
| 2026-10-16 | Next to AP | Browser marker | Loss 50/min → 0, RTT ~4 ms; input lag unchanged |
| 2026-10-16 | Capture method | Browser marker | WGC 185.5 ms vs DDX 188.8 ms |
