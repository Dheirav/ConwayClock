# Running a Game-of-Life clock as a wallpaper with minimal resources

Written 2026-09-04 from three parallel research passes (host, engine,
precomputation) plus measurements taken in this repo. Figures are marked
**measured** (by me or by the research pass, on named hardware),
**reported** (someone else's number, linked) or **estimate**.

## What we are running

dim's digital clock (Code Golf Stack Exchange, 2017): a 10,016 x 6,796
cell Life machine, 245k cells at generation 0 settling to 367-387k, one
displayed minute per 11,520 generations, so real time is 192 generations
per second. The full 12-hour cycle is 8,294,400 generations. It is
*functionally* periodic but not bit-exact: after 12 h about 6,000 cells
differ, nearly all in one 512 x 512 block that holds the PM-indicator latch
(measured with lifelib). So wall-clock sync must reload a snapshot at the
wrap rather than assume the state repeats.

The only known alternative pattern, VladanMajerech's `clockMini.rle`, is
7,168 x 7,434 with 329k cells: not smaller in any way that matters, no
license, no documentation.

## Where the resources actually go (measured)

| Component | Cost | Notes |
|---|---|---|
| Simulation, this repo's JS Hashlife, warm | 2 ms per 64-gen step = 0.6 % of one core | typed-array engine, `hashlife.js` |
| Simulation, cold (first minute after start or cache flush) | 20-25 ms per step, ~7 % of one core | ~25 s of CPU per simulated minute |
| Engine memory | 250-300 MB | node tables + memo; compaction keeps it bounded |
| Render, whole machine at 1/8 scale, tile-cached | < 1 ms per frame | |
| Startup sync from the nearest 10-minute snapshot | 22-66 s | measured across six times of day |
| **WebView2 host (Lively web wallpaper)** | **~350 MB; 10-20 % of one core at 30 fps; ~1.5 % hidden** | measured on your machine with the earlier design |
| bgolly (Golly's C++ Hashlife) on the same pattern | 0.26 ms per 64-gen step, **123 MB**, ~0.06-0.6 % of one core | measured by the research pass, Ryzen 7 7435HS |
| lifelib (C++/Python, MIT) | 0.77 ms per 64-gen step, **88 MB**, cold minute 1.5 s | measured by the research pass |
| copy.sh/life (JS, object nodes) | 0.6-2.8 ms per 64-gen step, 400-600 MB RSS, GC stalls up to 0.5 s | measured by the research pass |

The conclusion that falls out: **the simulation is already cheap; the
browser is the cost.** Nothing done to the engine changes the 300 MB and
the compositor work that WebView2 charges for existing.

## Options, ranked by runtime resources

### 1. Native host: a small app parented behind the desktop icons (estimate: 20-60 MB total, 1-2 % of one core, GPU idle)

How: a Win32 window reparented under the `WorkerW` that Progman spawns
(the same trick Lively uses; ~30 lines, examples in C, Rust and C++:
weebp, wallpaper-rs, d3d9-animation-wallpapers). Draw with a D3D11
flip-model swapchain and present only when a new generation arrives, 3-6
times a second. Embed the engine: bgolly's `gollybase` (GPL) or lifelib
(MIT, x86-64 only, 88 MB measured) or a C port of `hashlife.js`.
Occlusion: DXGI's occluded status is unreliable; do what Chromium does and
enumerate windows in z-order to find whether the desktop is visible, plus
session-lock and display-off notifications and battery state.

Catch: highest effort (1-2 days for a first version, then multi-monitor,
DPI and Windows 24H2 edge cases). If run *through* Lively's "application"
wallpaper type you lose Lively's pause, which is a no-op for that type.
All footprint figures are estimates; no one has measured this exact app.

This is the only option that stays a real computing machine *and* removes
the host cost.

### 2. Stay in WebView2, but starve it (estimate: ~300 MB, 2-5 % of one core)

What `index.html` already does: timer-driven repaint at 3-24 fps, a small
canvas scaled by the GPU, a frame submitted only when the picture changed.
Extra levers: `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` reaches Lively's
player without code changes, but the useful flags all go the wrong way
(`--disable-gpu-vsync` uncaps frame rate); Lively's "pause when another
application is focused" gives ~0 when you are working. Lowest effort,
already built. The 300 MB stays.

### 3. A 12-hour recording (estimate: 0.5-3 GB on disk, 0.5-3 % of one core, 100-250 MB)

Render every frame once (a few hours overnight), encode AV1 or HEVC with
screen-content tools so the hundreds of identical glider streams dedupe,
play it through Lively's mpv with a five-line Lua script that seeks to
`time-of-day mod 12 h` on load. Hardware decode is under 10 % of the energy
of software decode; a Framework 13 measured mpv 1080p hardware-decoded at
about 4 W above idle, which is more than a native app presenting 3 frames a
second. It is a recording of the real machine, visually identical, but it
no longer computes anything, and every change means re-rendering.

Segmented or composited variants (background loop + digit-transition
clips, 50-100 MB) and tile playback (~20 MB of data, ~1 % CPU in a native
renderer) buy disk space, not CPU, at the cost of 3-5 days' work.

### 4. Wall-clock-driven digits over a looping background (estimate: 2-5 %, tens of MB)

Cheapest of all and a fake: the machine in the background loops, the time
is drawn by the wallpaper from the system clock. Listed for completeness.

### 5. GPU bitmap simulation (rejected)

13 billion cell-updates per second is within an RTX 4060's reach, but it
keeps the discrete GPU awake at 192 dispatches a second, several watts
continuously, for a job Hashlife does at under 1 % of one core.

## Recommendation

Keep the current page while you decide whether the machine is what you
want on your desktop; it costs the WebView2 baseline plus a few percent of
one core and needs no more work.

If you want the minimum, build option 1: a native WorkerW app with an
embedded Hashlife, presenting on generation ticks. Expected saving versus
the page: roughly 250-300 MB of RAM and most of the compositor CPU. It
does not make the machine itself any cheaper, because that is already at
the noise floor.

Two cheap improvements apply either way:

- **Ship a warmed cache.** Cold start costs ~25 s of CPU per simulated
  minute because the memo has to learn the machine. bgolly does the same
  work in 1.5-2.8 s, so either port the engine to C for the native app or
  pre-warm in a worker.
- **Cap the memo.** Golly keeps this pattern in 123 MB; the JS engine
  uses about twice that. A hard cap plus more frequent compaction would
  bring it close.

## Sources

Host: Lively source (`WinDesktopCore.cs`, `VideoMpvPlayer.cs`,
`ExtPrograms.cs`, `Form1.cs`), Lively wiki (Performance, Video Guide,
Command Line Controls, Application Wallpaper), Wallpaper Engine docs and
forum threads, weebp / wallpaper-rs / YunuWallEngine / AnyWebWall repos,
Microsoft DXGI 1.2 and WebView2 flag docs, Chromium's window-occlusion
design doc, Reupen's 2026 note on DXGI occlusion.
Engine: AlephAlpha/golly (bgolly v5.1b1), Rokicki's hashlife notes and
G4G13 algorithm shootout, python-lifelib, copy/life, smeagol and
hashlife-rust crates, Fišer's CUDA Life, nullprogram's WebGL Life.
Precomputation: streaming-codec bitrate comparisons (SLC, Visionular AV1
SCC), arXiv 2402.09001 on hardware-decode energy, Chromium VideoNG,
whatwg seek-accuracy thread, mpv seamless-loop PR 10748, Lively CLI docs.
