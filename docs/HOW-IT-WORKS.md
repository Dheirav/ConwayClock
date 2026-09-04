# How it works

## The machine

| Pattern | 10,016 x 6,796 cells; about 245,000 alive at generation 0, 340,000 to 400,000 running |
|---|---|
| One displayed minute | 11,520 generations |
| Real time | 192 generations per second |
| Full cycle | 24 hours = 16,588,800 generations; the PM indicator box is lit for the first 12 hours and dark for the second |

`clock.rle` is dim's pattern from the anonymous gist linked from the Stack
Exchange answer. Generation 0 shows 12:00 PM. The machine is functionally
periodic over 24 hours but not bit-exact: about 6,000 cells differ after a
cycle, nearly all in the PM latch region, so the program never assumes the
state repeats.

## The engine

`native/hashlife.c` is Hashlife, Gosper's memoised quadtree. Every node is
canonical, so identical regions of the grid share one node, and each node
caches its own future: the centre of the node advanced 2^j generations.
A machine like this one is almost entirely periodic at the small scale, so
after a minute of warm-up nearly every step is a cache hit.

Implementation notes: nodes are 28-byte structs in one flat array with the
result cached inline (one slot suffices because results for j >= level-2 do
not depend on j, and nodes high enough for j to matter are fresh every
step); an open-addressing table for canonicalisation; a tile cache for
rendering, so a periodic machine renders almost entirely from cache; and a
compaction pass that keeps only what the current state reaches.

Measured, warm, whole machine: 0.5 to 1 ms per 64-generation step, under
1 ms to render at 1/8 scale, 100 to 150 MB. A cold start (first simulated
minute) costs 2 to 3 s. `native/test_hl.c` checks the engine against a
naive simulator; its bitmap after 90 steps is byte-identical to the
JavaScript engine that generated the snapshots.

## Wall-clock sync

Minute M of the machine's cycle is M minutes after noon. The target
generation is M x 11,520 + 11,450 + seconds x 192. The 11,450 is the
measured time the display takes to settle after its counter ticks, chosen
so the display is never early.

144 precomputed states, one per 10 minutes of the cycle, are embedded in
the executable (`native/snapshots_data.c`, generated from
`snapshots/*.rle.gz`). On start the nearest earlier one is loaded and
advanced to the target, typically in one to two seconds. If the clock
jumps backwards or the program falls more than half an hour behind, it
resyncs from a snapshot.

## Behind the icons on Windows 11

The classic trick, parenting a window under the WorkerW that Progman spawns
on message 0x052C, no longer applies on Windows 11 24H2 and later. Lively's
24H2 method, a layered child of Progman placed beneath `SHELLDLL_DefView`,
does not work on build 26200 either: Progman has
`WS_EX_NOREDIRECTIONBITMAP` and its child windows are not composed at all.
Eight attachment strategies were tested with automated screenshots. The
one that works is parenting the window inside `SHELLDLL_DefView` at the
bottom of its children, beneath the icon list. DefView is a layered window
whose surface is composed with per-pixel alpha, so the pixels must carry
opaque alpha or the blend turns additive. `attach = N` selects the other
strategies for other builds, and a watchdog recreates and re-attaches the
window if Explorer restarts.

## Pausing

Once a second the program walks the top-level windows in z-order and
subtracts each visible, uncloaked, non-minimised, opaque one from the
screen until it reaches Progman. If nothing is left, the desktop is covered
and stepping stops. Click-through overlays and translucent windows are
ignored. Session lock and display-off come from Windows notifications. On
the 24H2 "raised desktop", Show Desktop lifts Progman above the apps
instead of minimising them, which the walk handles by stopping at Progman.

## Rendering

Each frame: a density map of the view at 1/8 scale from the tile cache,
a separable bilinear resample to the screen (each source row scaled once,
then rows blended in a streaming pass), a palette lookup into the window's
bitmap, and one GDI blit of the machine's rectangle. The AM/PM dot and the
colon replacement are the only additions to the picture; both are driven
by the machine's own state (the dot by the indicator box's cell count, the
colon by swapping the still-life discs for pulsar discs at load, in a
region nothing else ever enters).

## Cost

Profiled on an RTX 4060 laptop, 1080p at 125 %, Windows 11 build 26200,
with the desktop visible, process CPU sampled over 42 s:

| Setting | CPU, one core | Per frame | Memory |
|---|---|---|---|
| 6 fps | 3.8 to 4.2 % | render 0.6 to 1.0 ms, resample 3.2 to 3.5 ms, blit 0.9 to 1.0 ms | 97 MB working set |
| 3 fps | about half | same per frame | same |
| Desktop covered, locked or display off | 0 % | no frames | same |

GPU: none measurable; the program draws on the CPU into a bitmap that
Windows composites like a static wallpaper. For comparison, the same clock
as a Lively web wallpaper cost about 350 MB and 10 to 20 % of a core with a
busy GPU process, and a hardware-decoded video wallpaper runs several watts
above idle.

## Building

From WSL with Zig, no admin, no Windows toolchain:

```bash
Z=~/.local/opt/zig-x86_64-linux-0.14.1/zig
cd native && $Z cc -target x86_64-windows-gnu -O2 -o life-clock.exe \
  main.c hashlife.c inflate.c colon.c snapshots_data.c \
  -lgdi32 -luser32 -ldwmapi -lwtsapi32 -lshell32 -Wl,--subsystem,windows
```

`snapshots_data.c` is generated from `snapshots/*.rle.gz` by
`tools/gen-snapshot-data.py`. To regenerate the snapshots themselves
(needs Node): `tools/gensnapshots.js` with `START` and `END` minutes writes
one every 10 minutes, about 6 s each; `tools/snapshot-progress.sh --watch`
shows progress with an ETA from the measured rate. `tools/hashlife.js` is
the JavaScript engine those tools use and `tools/reader.js` reads the
7-segment display from a universe, for tests.

The Linux-side tests: `gcc -O2 -o test_hl native/test_hl.c native/hashlife.c`
runs the naive comparison, the population trace and the timings.
