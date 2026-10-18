# Developing Vita Moonlight

This guide covers building the app, deploying builds to a real Vita over Wi-Fi, reading logs and crash dumps, and testing changes. It's written so that a developer, or an automated agent, can go from a fresh clone to a build running on the Vita without guesswork.

## Quick start

```sh
git clone --recurse-submodules <repo>
cd vita-moonlight
tools/vita.sh build                    # build/eboot.bin and build/moonlight.vpk
export VITA_IP=192.168.1.50            # your Vita's address
tools/vita.sh push                     # quit the app, upload eboot.bin, relaunch
tools/vita.sh log                      # print moonlight.log
```

`push` needs the one-time Vita setup below.

## Building

### With Docker (recommended)

`tools/vita.sh build` builds inside a pinned `vitasdk/vitasdk` Docker image, so nothing needs installing on the host apart from Docker. The image is multi-architecture (`linux/amd64` and `linux/arm64`), so it runs natively on Intel and Apple Silicon machines.

The build output in `build/` is owned by your user, not root.

The image is pinned by digest in two places, and they must match:
- `tools/vita.sh` (`DEFAULT_IMAGE`)
- `.github/workflows/cmake-psvita.yml` (`container.image`)

To try a different image, set `VITASDK_IMAGE`:

```sh
VITASDK_IMAGE=vitasdk/vitasdk:latest tools/vita.sh build
```

Update the pin deliberately, and check the build still links: SDK package changes have broken it before (see below).

### Without Docker

Install [VitaSDK](https://vitasdk.org/), then:

```sh
git submodule update --init --recursive
cmake -S . -B build
make -C build -j8
```

The SDK must provide OpenSSL **1.0.2**. See the next section.

### Why the build steps are the way they are

| Symptom | Cause | Fix in the tree |
|---|---|---|
| `implicit declaration of function 'strdup'` | GCC 14 and later make implicit declarations errors, and `-std=c99` hides POSIX functions in newlib | Built with `-std=gnu99` |
| Undefined `sk_num`, `SSLeay`, `SSLv23_client_method`, `EVP_MD_CTX_create`... at link time | The SDK image ships OpenSSL 1.1.1 (required by its `cpython3` package), but its prebuilt `libcurl` is linked against **1.0.2** | The build swaps in the `openssl` (1.0.2) vdpm package inside the container. `vdpm install openssl` refuses because of the `cpython3` dependency, so it removes 1.1.1 with `vdpm pacman -- -Rdd` first. |
| Undefined `ZSTD_*` | The SDK's `libcurl` uses zstd | `-lzstd` is linked after `-lz` |

## One-time Vita setup

You need a Vita with HENkaku (or Ensō) and taiHEN, and VitaShell.

1. **Install the app once.** Copy `build/moonlight.vpk` to the Vita and install it with VitaShell. Its title ID is `XYZZ00002`. After this, deploys only replace `eboot.bin`.
2. **Install vitacompanion.** It gives the Vita an FTP server and a command server on the local network. Copy both files from a [vitacompanion](https://github.com/devnoname120/vitacompanion) release to `ur0:tai/`:
   - `vitacompanion.suprx`, a user plugin: FTP on port **1337**
   - `vitacompanion_kernel.skprx`, a kernel plugin: commands on port **1338**
3. **Register both plugins in `ur0:tai/config.txt`.** Edit `ux0:tai/config.txt` instead if that file exists, because taiHEN reads it first. Add the kernel plugin under a `*KERNEL` section and the user plugin under `*main`:

   ```
   *KERNEL
   ur0:tai/nonpdrm.skprx
   ur0:tai/vitacompanion_kernel.skprx
   *main
   ur0:tai/henkaku.suprx
   ur0:tai/vitacompanion.suprx
   ```

4. **Fully reboot the Vita.** Power it off and on, then re-apply HENkaku if you don't have Ensō. "Reload taiHEN config.txt" is not enough, because `*main` plugins load into the home screen process, which is already running, and kernel plugins only load at boot. Check that Wi-Fi reconnects afterwards; it may not do so on its own.
5. **Check from your computer:**

   ```sh
   nc -z $VITA_IP 1337 && nc -z $VITA_IP 1338 && echo ready
   printf 'version\n' | nc -w 3 $VITA_IP 1338    # vitacompanion 1.08 at the time of writing
   ```

**Security:** vitacompanion's ports have no authentication. While it's loaded, anything on your network can read and write the Vita's storage and launch or quit apps. Comment the two lines out of `config.txt` and reboot when you're not developing.

## The deploy loop

```sh
tools/vita.sh build       # in Docker
tools/vita.sh push        # quit, upload, relaunch
tools/vita.sh all         # both
tools/vita.sh launch      # start the app
tools/vita.sh kill        # quit the app
tools/vita.sh log [FILE]  # fetch moonlight.log
tools/vita.sh crash       # fetch the newest core dump into build/
```

`push` does three things:
1. Sends `quit XYZZ00002` to the command port.
2. Uploads `eboot.bin` over FTP to `ux0:/app/XYZZ00002/eboot.bin`, retrying for a few seconds while the app exits.
3. Sends `launch XYZZ00002`.

The elevated permissions that PS button capture needs are inside `eboot.bin` (`vita-make-fself -a 0x2800000000000001`), so replacing it keeps them. Changes to `sce_sys/` (LiveArea, `param.sfo`) or `assets/` still need the VPK reinstalled.

### Sharing a Vita between sessions

**Why:** when several sessions (people, terminals or agents) drove one Vita at once, they relaunched the app under each other and deployed different builds in the middle of each other's tests. The overload eventually froze the Vita outright, and under heavy screenshot polling its FTP server stopped transferring.

**The lock:** so sessions take turns, every device command takes a lock first and waits for it (up to 30 minutes), printing who holds it. That covers `tools/vita.sh` (everything except `build`; `all` takes it only for the push), `tools/bench/run.sh` (for the whole run) and `tools/bench/shot.sh`. Nobody has to remember anything.

- **One lock per clone.** It's the directory `vita.lock` in the clone's `.git` directory, shared by all its worktrees. Set `VITA_LOCK` to a path to share one across clones.
- **Who holds it:** `tools/vita-lock.sh --status`. Name yourself for others with `VITA_LOCK_OWNER` (the default is `user@host:pid`).
- **For a batch of steps** (a deploy, button presses with `nc`, then screenshots), wrap the whole batch so nobody gets in between. The tools inside see the lock is held and don't wait for it again:

  ```sh
  VITA_LOCK_OWNER=settings-check tools/vita-lock.sh ./my-batch.sh
  ```

- **Release:** the lock is released when the command ends, or on Ctrl-C. Killing only `vita-lock.sh` releases it once its command has ended, so kill the process group to stop both. A lock whose holder hasn't been seen for 20 minutes counts as abandoned; holders refresh it every minute, so a long benchmark keeps it.
- **On your own,** turn it off with `VITA_LOCK=off`.

Keep each turn short, and redeploy (checking the hash) at the start of a batch, since the installed `eboot.bin` may be someone else's.

### vitacompanion commands

Send one command per connection, ending in a newline, for example `printf 'launch XYZZ00002\n' | nc -w 3 $VITA_IP 1338`. Version 1.08 lists:

| Command | Effect |
|---|---|
| `help` | Lists the commands. **Never type `help <command>`**: it runs that command (`help quit` quit every app). |
| `launch <TITLEID>` | Starts an app |
| `quit [TITLEID]` | Quits an app, or **all apps** with no argument. Replies "Error: cannot quit the app" if it isn't running. |
| `reboot` | Reboots the Vita |
| `screen on` / `screen off` | Turns the screen on or off |
| `nosleep on`, `off`, `status` | Prevents suspending (not screen dimming) |
| `press`, `release` | Synthetic input, e.g. `press cross; wait 100ms; release cross`. See [`REMOTE-VITA.md`](REMOTE-VITA.md) |
| `promote` | Promotes an extracted app directory |
| `wait` | Waits for a duration such as `100ms` or `3s` |
| `version` | Prints the version |

There is no `destroy` command; older guides that mention it refer to an earlier version.

**Driving the Vita without touching it** (button presses, safe config edits, screenshots, getting measurements back) is covered in [`REMOTE-VITA.md`](REMOTE-VITA.md).

## Logs and crash dumps

- **Config:** `ux0:data/moonlight/moonlight.conf`. If `ux0:` can't be used it falls back to `uma0:data/moonlight/`, then to `ux0:data/` (see `src/check_dir.c`).
- **Log:** `moonlight.log` in the same folder, written only when **Settings → System → Enable debug log** is on. The log slows streaming (an allocation, a lock and a memory card flush per line), so turn it off for normal play and for performance comparisons that don't need it.
- **Useful log lines:**
  - `[STATS]`: once a second, with the debug log on
  - `[THREAD]`: thread priority, cores and CPU, once per stream
  - `Clocks boosted`
- **Crash dumps:** `ux0:data/*.psp2dmp`. Fetch the newest with `tools/vita.sh crash`, then decode it against the matching ELF with `vita-parse-core build/<dump>.psp2dmp build/moonlight.elf`.

## Testing

### On-screen stats

**Settings → System → Stats overlay**, set to `full`, shows the following, refreshed once a second:
- an estimated end-to-end latency
- frame rates and lost frames
- network, queue, decode and render times
- round-trip time
- Wi-Fi signal and channel
- clock speeds
- the overlay's own CPU cost

It can be changed from the pause menu mid-stream (START + L + R, then Settings).

### A reproducible target

Stream `tools/bench/bench.html` from the Sunshine host, so runs see identical content. See [`tools/bench/README.md`](../tools/bench/README.md) for setting it up as a Sunshine app on Windows, macOS or Linux.

### Testing several branches together

Stacked or parallel branches can be tried on the device at once by merging them into a local branch that is never pushed. For example, from a separate worktree:

```sh
git worktree add .worktrees/test fix/build-current-vitasdk
cd .worktrees/test
git checkout -b test/integration
git merge feature-a feature-b feature-c
tools/vita.sh build && tools/vita.sh push
```

When a branch changes, merge it into `test/integration` again and rebuild.

## Continuous integration

`.github/workflows/cmake-psvita.yml` builds every pull request, and every push to `vita`, in the same pinned image. It uploads `moonlight.vpk` and `eboot.bin` as an artifact.

On a fork, GitHub disables workflows until you enable them in the fork's **Actions** tab. To fetch a CI build instead of building locally, run `gh run download`.

## Things to know when changing the code

- **Line endings vary:** some files use CRLF, for example `src/input/touchabsolute.c`, `src/keyboardsystem.c` and `src/gui/ui_keyboard.c`. Keep each file's existing endings, and check `git diff --stat` for whole-file changes.
- **The UI font has no PlayStation symbols.** The font, mononoki Nerd Font, has no △, ○, ✕ or □ glyphs, and they render as empty boxes. Draw control symbols with `draw_button_glyph()` (in `src/gui/guilib.c`) instead of putting them in strings.
- **The settings menu is a fixed-size array** in `ui_settings_menu()`, with about 1 KB of the main thread's stack per row. Keep its size and the count comment above it in step when adding rows.
- **`display_menu()` needs at least one selectable row,** and it only scrolls by moving the cursor.
