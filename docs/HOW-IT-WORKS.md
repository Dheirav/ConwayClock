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
generation is M x 11,520 + 12,800 + seconds x 192. The 12,800 comes from
measuring, for a sample of minutes across the cycle, the generation range
in which the display reads that minute exactly: it settles between +14,208
and +18,816 and starts changing again between +19,328 and +22,912. A lag
between 11,392 and 14,210 therefore covers every one of those windows
without ever settling on the previous or the next minute, and 12,800 is
its centre.

`native/sweep_lag.c` later checked that constant against the whole cycle
rather than a sample: it walks all 24 hours at 64-generation resolution,
recording what the display reads, and then scores every candidate lag
against that one pass, since a lag only chooses which of those readings the
wallpaper would have shown. The answer is that 12,800 is right -- 29.6
seconds of each minute correct against a best-possible 29.8 at 11,584, on a
plateau from about 11,300 to 14,300. `tools/lag-progress.sh` watches the
sweep, which takes 20 to 35 minutes; `SWEEP_CACHE=<file>` saves the readings
so that re-scoring costs nothing.

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
window once Explorer has rebuilt the desktop after a restart (tested: it
re-attaches under the new shell view within a second). The program also
verifies itself: once the
desktop has been visible for a few seconds it captures a patch of the
screen at the digits' centre and compares it with its own bitmap; if fewer
than 70 % of the pixels match it moves to the next strategy, so a future
Windows build that changes the layout again degrades to a logged fallback
rather than a blank desktop. The check is skipped while another window,
such as a topmost call window, covers the sample point, and if no strategy
verifies it returns to the default and tries the cycle again in five
minutes.

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
bitmap, and one GDI blit of the machine's rectangle.

The map is mostly empty -- measured on the default view, 13 % of its 8x8
tiles hold anything, and a single interval per row covers 33 % of it -- so
the engine reports, per map row, the columns its descent touched, and the
resample runs over the union of this frame's spans and the last frame's
rather than the whole rectangle. What that union leaves alone keeps the
pixels it already had. Anything that invalidates untouched pixels forces a
whole-picture frame: a resize, a pan, and in particular a day/night fade,
where the background colour itself moves. `highlight` and `afterglow` are
whole-map passes in their own right and fall back to the old path.
Tracking which tiles *changed* rather than which are occupied would not
help: 95 % of the live tiles differ every frame, because nearly everything
alive in this machine is a glider in flight.
`native/test_hl.c` checks that the span-built map is byte-identical to a
full rebuild and that the span-to-column arithmetic is never too narrow;
`--frame out.bmp --selfcheck` renders on Windows both ways at the same
generation and logs the pixel difference and the cost of each. Measured on
the 1080p default view, per frame: 2.01 ms partial (render 0.77, resample
1.24) against 4.04 ms whole-picture (render 0.66, resample 3.38), with no
pixel differing. The resample falls to 37 % of its former cost and the span
bookkeeping adds about 0.11 ms to the render, so a frame's render and
resample together halve.

The saving is a property of the zoomed-out view, where most of the map is
empty. At `zoom = 4`, the watch view, the same measurement gives 2.31 ms
against 2.40 ms: the crop is dense, there is little empty space to skip, and
the partial repaint is worth nothing. That is the right way round, since the
wallpaper is what runs all day.

Confirmed in ordinary use rather than on a bench, by running both builds as
the wallpaper on the same machine at `fps = 12` and reading the per-minute
log line:

| | frames in the minute | CPU, one core | resample |
|---|---|---|---|
| before | 626 to 636 | 7.3 to 9.2 % | 3.2 to 3.7 ms |
| after | 640 to 643 | **6.4 to 6.5 %** | **1.2 ms** |

Minutes that follow a long pause cost more than this in total while the
machine catches up -- the engine, not the drawing -- so compare only minutes
with a full frame count. The palette actually drawn eases toward
the configured one, so a day/night switch fades over `fade` seconds instead
of jumping. Optional passes on the density map: afterglow (a decaying maximum per pixel) and highlight (a
decaying record of how much each pixel changed, selecting a second
palette). The AM/PM dot and the colon replacement are the only additions
to the picture; both are driven
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
  main.c hashlife.c inflate.c colon.c settings.c install.c snapshots_data.c life-clock.rc \
  -lgdi32 -luser32 -ldwmapi -lwtsapi32 -lshell32 -lole32 -luuid -lcomctl32 -lcomdlg32 -ladvapi32 -Wl,--subsystem,windows
```

`snapshots_data.c` is generated from `snapshots/*.rle.gz` by
`tools/gen-snapshot-data.py`; `life-clock.ico` (the 7-segment icon compiled
in through `life-clock.rc`) by `tools/gen-icon.py`. To regenerate the snapshots themselves
(needs Node): `tools/gensnapshots.js` with `START` and `END` minutes writes
one every 10 minutes, about 6 s each; `tools/snapshot-progress.sh --watch`
shows progress with an ETA from the measured rate. `tools/hashlife.js` is
the JavaScript engine those tools use and `tools/reader.js` reads the
7-segment display from a universe, for tests.

The Linux-side tests, both run by CI on every push:

- `native/test_hl.c` checks the engine against a naive simulator, prints the
  population trace that must match the JavaScript engine, and times it.
- `native/test_clock.c` is the end-to-end one: for a sample of minutes across
  the 24-hour cycle it loads the embedded snapshot, advances to the
  generation the wallpaper would use, reads the seven-segment display back
  off the grid and compares it with the clock, including AM/PM. It fails if
  the sync is a minute out or if the cycle is read as 12 hours instead of 24,
  which is the bug that shipped in an early version.
