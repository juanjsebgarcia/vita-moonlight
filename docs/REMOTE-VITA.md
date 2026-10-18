# Driving a Vita from your computer

How to deploy to a Vita, operate it, and get measurements and screenshots back without touching it. Everything here goes over the local network through [vitacompanion](https://github.com/devnoname120/vitacompanion): FTP on port **1337** for files, a command server on port **1338** for apps and input.

Start with [`DEVELOPMENT.md`](DEVELOPMENT.md) for building and the one-time Vita setup. This guide picks up from there. The tools it uses arrive with several PRs, listed in [`FORK-CHANGES.md`](FORK-CHANGES.md):
- **#5:** `tools/vita.sh`
- **#9:** `tools/bench/`
- **#10:** developer mode, the benchmark, frame grabs, packet counters and screenshots

```sh
export VITA_IP=192.168.1.50      # your Vita's address (Settings > Network on the Vita)
nc -z $VITA_IP 1337 && nc -z $VITA_IP 1338 && echo ready
```

**If others share your Vita,** `tools/vita.sh`, `run.sh` and `shot.sh` queue for a lock by themselves. Wrap a sequence of your own commands, such as the presses and screenshots in section 7, in `tools/vita-lock.sh`, so nobody gets in between. See "Sharing a Vita between sessions" in `DEVELOPMENT.md`.

## 1. Build and deploy

```sh
open -a Docker                    # macOS: the build runs in Docker
tools/vita.sh build               # check it exits 0, and grep its output for "error:"
tools/vita.sh push                # quit the app, upload eboot.bin, relaunch
```

**Check that the Vita really has the new build.** A build that fails quietly leaves the old `eboot.bin` in `build/`, and `push` uploads it anyway:

```sh
curl -s ftp://$VITA_IP:1337/ux0:/app/XYZZ00002/eboot.bin | shasum
shasum build/eboot.bin            # the two must match
```

**To test several branches together,** merge them into a local scratch branch in a separate worktree and build there. See "Testing several branches together" in `DEVELOPMENT.md`. Never push that branch. Merge one branch at a time, and check for conflict markers (`git grep -n '^<<<<<<< '`) before building.

## 2. Commands: apps, screen, power

One command per connection, ending in a newline:

```sh
printf 'launch XYZZ00002\n' | nc -w 3 $VITA_IP 1338
printf 'quit XYZZ00002\n'   | nc -w 3 $VITA_IP 1338   # "Error: cannot quit the app" when it isn't running
```

| Command | Notes |
|---|---|
| `launch XYZZ00002`, `quit XYZZ00002` | Start and stop Moonlight. `quit all` quits every app |
| `screen on`, `screen off` | The display |
| `nosleep on`, `off`, `status` | Prevents the Vita **suspending**. It does not stop the screen dimming or turning off (see section 6) |
| `press`, `release`, `wait` | Synthetic input (section 3) |
| `help` | Lists the commands. **Never** type `help <command>`: it runs that command. `help quit` quit every app |

## 3. Pressing buttons

```sh
printf 'press cross; wait 100ms; release cross\n' | nc -w 3 $VITA_IP 1338
```

- **Button names:** `select start up right down left l r l1 r1 l2 r2 l3 r3 triangle circle cross square ps` (`x` is an alias for `cross`). There are also sticks (`press left-stick 0 128`, 0 to 255 with 128 the centre) and touch (`press front-touch 0 960 544`, in the raw 1920x1088 touch space).
- **Send a whole sequence in one command.** The main menu rebuilds itself whenever the host scan reports a change, and each rebuild puts the cursor back at the top, so presses sent one at a time can land on the wrong row:

  ```sh
  tap() { local c=""; for b in "$@"; do c="$c press $b; wait 70ms; release $b; wait 150ms;"; done
          printf '%s\n' "$c" | nc -w 5 $VITA_IP 1338; }
  tap down down down cross        # main menu: from your PC, down to Settings, open it
  tap r1                          # next Settings tab
  ```

- **L and R:** the menus read buttons with `sceCtrlPeekBufferPositive`, which reports the shoulder buttons on the **L2/R2 bits**. `l1` and `r1` arrive as those bits too. So code that reacts to L or R must check `SCE_CTRL_L1 | SCE_CTRL_L2` (and the same for R).
- **Confirm and cancel** follow the "Swap X & O" setting (`config.btn_confirm` / `btn_cancel`). The defaults are cross to confirm and circle to cancel.

## 4. Files: config, logs, caches

Moonlight keeps everything in `ux0:data/moonlight/`:

| Path | What |
|---|---|
| `moonlight.conf` | Settings |
| `moonlight.log` | The debug log, written only with Settings → System → Enable debug log on |
| `reports/last-bench.json`, `reports/bench-*.json` | Benchmark reports |
| `reports/grab-<tick>.raw` | Frame grabs from the last benchmark |
| `shot.raw` | The last developer screenshot |
| `<PC name>/` | Pairing keys and `device.ini` for each PC |
| `boxart/<PC name>/<app id>.png` | Cached box art |
| `ux0:data/*.psp2dmp` | Crash dumps (system-wide) |

```sh
curl -s ftp://$VITA_IP:1337/ux0:/data/moonlight/moonlight.conf -o moonlight.conf      # download
curl -s -T moonlight.conf ftp://$VITA_IP:1337/ux0:/data/moonlight/moonlight.conf      # upload
curl -s -Q "DELE ux0:/data/moonlight/shot.raw" ftp://$VITA_IP:1337/ -o /dev/null      # delete
```

**Change settings only while Moonlight is closed.** It writes `moonlight.conf` back when you leave Settings, so an upload while it's running can be overwritten. Quit it, back the file up, edit it, upload it, and relaunch:

```sh
printf 'quit XYZZ00002\n' | nc -w 3 $VITA_IP 1338; sleep 2
curl -s ftp://$VITA_IP:1337/ux0:/data/moonlight/moonlight.conf -o conf.orig
python3 tools/bench/conf_edit.py conf.orig conf.new developer_mode=true save_debug_log=true
curl -s -T conf.new ftp://$VITA_IP:1337/ux0:/data/moonlight/moonlight.conf
printf 'launch XYZZ00002\n' | nc -w 3 $VITA_IP 1338
# ... and upload conf.orig again when you're done
```

## 5. Getting information back

| You want | How |
|---|---|
| **What the app is doing** | Turn the debug log on (it slows streaming; turn it off for timing comparisons), then `tools/vita.sh log out.log`. Useful lines: `[STATS]` (once a second), `[THREAD]`, `[BENCH]`, `[BOXART]`, `[UI]`. moonlight-common-c's own messages, such as `Unrecoverable frame` and `Waiting for IDR frame`, land here too |
| **Why it crashed** | `tools/vita.sh crash` fetches the newest `.psp2dmp`, then run `vita-parse-core build/<dump>.psp2dmp build/moonlight.elf` with the ELF from the same build |
| **Live numbers on screen** | Settings → System → Stats overlay: fps, or full (latency estimate, bitrate, loss, timings, Wi-Fi) |
| **A screenshot of the menus** | `developer_mode = true` in the config (or hold L + R and press △ in Settings), then `tools/bench/shot.sh out.png`. It captures the next menu frame; the stream's video isn't captured |
| **Measurements of a stream** | The benchmark: `tools/bench/run.sh` runs settings variants unattended and prints a comparison table (`tools/bench/README.md`). Each report has input lag, the latency estimate, decode and render timings, the wait for the screen refresh, **real video packet loss** (`packets.lost`, `late`, `repeats`), freezes, Wi-Fi signal, network type and **battery drain** |
| **Pictures of a stream** | Run the benchmark with `bench_grab=true`. Decoded frames at fixed page ticks come back as PNGs, and `tools/bench/quality.py` scores them |
| **Any app, not the bench page** | Variants can set `bench_app=Desktop,bench_input=none` (no presses), and `bench_idr_hz=N` to request keyframes |

**Battery readings** need the Vita off the charger: reports say `"on_charger": true` when one was connected. For browser-marker input lag, prefer short runs, because over 10-minute runs the PC's browser seemed to throttle the page.

## 6. Keeping the Vita reachable

- **It dozes:** the Vita sleeps, or drops Wi-Fi, between runs. Turn off Settings → Power Save → Enter Standby Mode Automatically, and Wi-Fi power save, while testing. A loop that waits for ports 1337 and 1338 before starting is the practical pattern.
- **The screen turns itself off:** with nobody touching the Vita, it turns the screen off after a short while, and **the menus stop drawing**, because they wait for screen refreshes that don't come. vitacompanion's `screen on` didn't revive them. Developer mode avoids this: the screenshot hook tells the system someone's active, about twice a second. Streams aren't affected. Whether players hit this when they come back to an idle menu still needs checking by hand.
- **Synthetic input and FTP don't keep it awake,** and nor does `nosleep`.

## 7. Example: check a Settings screen remotely

```sh
# Developer mode on, as in section 4, then:
tap down down down cross          # open Settings from the main menu (cursor starts on your PC)
tools/bench/shot.sh stream.png    # the Stream tab
tap r1; tools/bench/shot.sh system.png
tap circle                        # back out
```

Each screenshot is a few short FTP sessions: one to ask for it, a directory listing a second until it's there, then one download. vitacompanion's FTP server stops transferring under heavier polling, so keep batches small.

If a screenshot times out, the app is probably stuck in something that doesn't redraw (a connection attempt, say), or the screen went off. Relaunch it and send the sequence again.
