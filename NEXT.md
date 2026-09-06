# Where this stands and what is open

Live handover document. Current state at the top, open work below it.
Updated 2026-09-05, at tag `v1.7`.

## State

The project has done the thing `RESEARCH.md` recommended. Option 1 — a
native Win32 host with an embedded Hashlife, parented behind the desktop
icons — is built and shipped: the C engine port, 144 embedded snapshots for
same-second startup sync, the `SHELLDLL_DefView` attachment with on-screen
self-verification and fallback cycling, occlusion pausing, tray menu,
settings window, screensaver mode, self-installer, day/night themes, and an
end-to-end test that reads the seven segments back off the grid.

Baseline verified on 2026-09-05, all four stages green:

| Stage | Result |
|---|---|
| `test_hl` against a naive simulator | `MATCH`; population trace matches the JS engine; 0.21–0.75 ms per 64-gen step warm |
| `test_clock`, 12 times across the cycle | 0 failed, 13.3 s |
| Windows build (Zig, `x86_64-windows-gnu`) | clean, 16,615,936 bytes |

Runtime figures below (4 % of one core, 97 MB, the per-frame split) are, unless
said otherwise, the
ones measured on the RTX 4060 laptop and recorded in
[`docs/HOW-IT-WORKS.md`](docs/HOW-IT-WORKS.md), not re-measured here.

What follows is not a defect list. It is where there is still room.

## 1. The resample is dense over a map that is mostly empty

**Value: highest.** This is the one number the project exists to minimise.

Per frame, from the profile in `docs/HOW-IT-WORKS.md`: render 0.6-1.0 ms,
**resample 3.2-3.5 ms**, blit 0.9-1.0 ms. The resample is roughly two
thirds of the cost.

Measured 2026-09-05 on the default 1920x1080 whole-machine view (1550x925
map at 1/8, 32 generations per frame), by walking the tile-level descent:

| | |
|---|---|
| live tiles per frame | 3,099 of 23,443 grid slots — **13.2 %** |
| of those, changed since the last frame | 2,944 — **95.0 %** |
| tile rows with any live content | 66 of 119, so 525 of 925 map rows |
| live span per row, one interval | 898 px of 1,550 (58 %) |
| live span per row, exact intervals | 379 px of 1,550 (24 %) |
| **map covered, one interval per row** | **33 %** |
| **map covered, exact intervals** | **14 %** |

Coordinates matter here and cost me a wrong answer once: `clock.rle` carries
`#CXRLE Pos=-7289,-1110`, but the snapshots the program actually loads carry
`Pos=0,0`, and `main.c` parses both with `use_pos=1`. A probe that measured
`clock.rle` with its own Pos header was looking mostly at empty space beside
the machine and reported 4.7 % live, 10.6 % covered. Measure on the
snapshots' 0-based coordinates, which is what `PAT_W`, `DISPLAY` and the view
rect in `main.c` all assume.

Two conclusions, and the first one is a trap worth recording:

**Tracking *change* is worthless here.** 95 % of the non-empty tiles differ
every frame — the machine is full of glider streams, so nearly everything
alive is also moving. Comparing canonical node ids frame to frame would
skip 5 % of the work. Do not build that.

**Emptiness is the exploitable property.** `cached_rec`
(`native/hashlife.c:179`) already prunes empty subtrees with
`if (POP(n) == 0) return`, which is why render is cheap. `render()`
(`native/main.c:186`) does not: pass 1 gathers every one of `view.ph` rows
across the full destination width, and pass 2 blends the full rect, over a
map where 87 % of tiles hold nothing. Restricting both to live rows x one interval per row
is 33 % of the current work; exact intervals, 14 %.

The change:

- `cached_rec` already computes `px, py` for each tile it writes. Have it
  record a per-tile-row column range as it goes — two compares per tile,
  ~1,100 tiles a frame. Expose it as `hl_density_spans`. It also lets the
  `memset(out, 0, w * h)` at `hashlife.c:196` shrink to last frame's spans.
- Pass 1 resamples only `[lo, hi)` of each live row, widened by one for the
  `r[k + 1]` bilinear tap. Empty rows are already zero in `hrows` and stay
  untouched.
- Pass 2 needs the output-column range for a source span, so `setup_view`
  gains an inverse of `mapX`, built where `mapX` already is.
- Repaint region is `prev_spans` union `cur_spans`, so a region that has
  just gone empty is cleared.

Because pixels outside the spans now persist between frames, these must
force a full repaint: `setup_view`, `dibPainted = 0`, a tour pan, a
resolution change, and `palette_ease()` — that last one matters, since the
background colour shifts during a day/night fade and every untouched pixel
would be stale.

Watch out: `highlight` and `afterglow` (`native/main.c:205-210`) are
unconditional full-map passes and would become the new bottleneck. Afterglow
is the awkward one — its decay must keep touching pixels that have gone
empty until they fade, so its span is a decaying union, not the live set.
Both default to off; the honest first cut keeps the full-map path when
either is on.

### Status, 2026-09-05

Steps 1 and 2 are implemented on `main`; step 3 needs a Windows machine.

- **Done.** `HlSpan` and `hl_density_spans` in `native/hashlife.h` /
  `hashlife.c`: `cached_rec` records each tile's clipped column range against
  the 8 map rows it covers. `hl_density_cached` is unchanged and still clears
  the map itself; the spans entry point deliberately does not, because with
  spans in hand the caller need only clear the previous frame's ranges.
- **Done.** `native/main.c`: `rowSpan` / `prevRowSpan`, the `invX` inverse of
  `mapX`, per-row output ranges in `oLo` / `oHi`, and both resample passes
  plus the `unitScale` path restricted to the union of this frame's and last
  frame's spans. `fullRepaint` forces the old whole-picture path and is set by
  `build_span_tables`, `create_dib`, `set_center` and `palette_ease`.
- **Done.** Two checks in `native/test_hl.c`, both in CI on Linux:
  the span-built map must be byte-identical to a full rebuild over 200 frames
  with no live pixel outside its row's span (passes, 0/200), and the
  span-to-output-column mapping must never be too narrow over 20,000 random
  spans (passes, 0/20,000).
- **Done: measured on Windows 2026-09-05** via `--frame out.bmp --selfcheck`,
  which now also times both paths. Default 1080p view, per frame: partial
  2.01 ms (render 0.77 + resample 1.24) against whole-picture 4.04 ms
  (render 0.66 + resample 3.38), no pixel differing over 32 frames. The
  resample lands at 37 % of its former cost, against 33 % predicted; the span
  bookkeeping costs about 0.11 ms of render. With the blit unchanged at
  0.9-1.0 ms a frame goes from ~5.0 ms to ~3.0 ms, so frame work at 6 fps
  drops from ~30 ms/s to ~18 ms/s.
- **Confirmed in production 2026-09-06.** Both builds run as the wallpaper on
  the same machine at `fps = 12`: resample 3.2-3.7 ms falls to 1.2 ms, and
  CPU at a full frame count falls from 7.3-9.2 % to 6.4-6.5 % of one core.
  Compare only minutes with ~640 frames; a minute following a pause is
  dominated by the engine catching up, not by drawing.
- **At `zoom = 4` it buys nothing**: 2.31 ms against 2.40 ms. The watch view
  is a dense crop with little empty space. Expected, and the right way round.
- **Exact intervals were tried and reverted, 2026-09-05.** One interval per
  row bridges the gaps in a row; a bitmap of the occupied 8-column blocks does
  not, and covers 14 % of the map against 33 %. Implemented in full and
  measured against the current version, alternating builds over three rounds
  to cancel machine load: resample 0.93 ms to 0.84 ms, but render 0.58 ms to
  0.68 ms for the block bookkeeping -- setting bits per tile, the union with
  the previous frame, and the row-pair union pass 2 needs. Total 1.51 ms
  against 1.52 ms: a wash. Do not build it again without a cheaper way to
  carry the blocks. A single measurement had suggested 0.78 against 1.24 ms;
  that was machine load, not the change, and only the alternating A/B showed
  it.
- **The self-check was wrong at first and is worth not repeating.** It
  rendered one partial frame and then a whole-picture frame at the same
  generation, in a loop -- so every partial frame followed a full one, its
  spans were unioned with a full-width previous frame, and the comparison was
  full against full. It reported 0 differences for the wrong reason. It now
  runs three partial frames before comparing, which is the minimum for the
  repaint to be genuinely narrow.

Watch for when measuring: `highlight` and `afterglow` deliberately fall back
to the whole-picture path, so measure with both off, which is the default.

## 2. Multi-monitor

**Done, and verified on real hardware 2026-09-06.** `monitor = 1` puts the
wallpaper on the second display.

The bug was that `attach_to_desktop` parented the window inside
`SHELLDLL_DefView` and then positioned it at `g_monX, g_monY`, which are
*screen* coordinates from `pick_monitor`. Once `SetParent` has made a window
a child, its position is in the **parent's client** coordinates. Those
coincide only for the primary monitor, where both are (0,0), which is why it
never showed. Child strategies now convert through `ScreenToClient` and the
top-level ones use the monitor's own origin.

**A premise recorded here earlier was wrong.** It said `SHELLDLL_DefView`
covers the primary monitor only, so a child of it could never be seen on
another display and the whole approach would need replacing. Measured with a
second monitor actually attached:

```
placement: window (1920,0)-(3840,1080); parent screen (0,0)-(3840,1080)
```

DefView spans the **whole virtual desktop**. It looked primary-only because
with one monitor that is the whole desktop. So the coordinate fix was the
entire fix, and no fallback was needed.

The fallback built alongside it -- `report_placement` returning whether any
of the window survives the parent's clip, and `detach_to_toplevel` dropping
`WS_CHILD` for a bottom-of-z-order window over the monitor -- never fires in
this configuration. It stays as insurance for a shell layout where DefView
does not cover a monitor, and it is exercised by building a binary that adds
the screen width to the monitor origin.

Not tried: two monitors at once. The program draws on one, chosen by
`monitor`. Spanning, or an instance per monitor, would still be new work, and
the single-instance mutex is in the way of the second.

## 3. The screensaver preview

**Done 2026-09-06.**

`main.c` parsed `/p`, threw away the window handle Windows passes after it,
and exited, so the little monitor in the screensaver dialog stayed black. It
now takes that handle, creates a plain `WS_CHILD` window inside it, syncs to
the current time and redraws at 6 fps until the dialog goes away. The
preview is not the wallpaper, so it does not take the single-instance lock.

The catch was DPI. The dialog need not share the program's per-monitor
awareness, so the client rect read from the parent and the client rect of the
window actually created can differ -- measured, 405x276 asked for against
506x345 received. The preview therefore re-reads its own client rect after
creation and re-runs `setup_view` when they disagree. Without that it painted
a 405x276 corner of a 506x345 window: 19.8 % of sampled pixels differed from
the background against 80.0 % once fitted.

Tested by creating a host window, passing its handle as `/p <hwnd>` exactly
as the dialog does, and checking a `LifeClockPreview` child appears and is
painted.

## 4. CI and the Windows code

**Done.** The job runs on a real runner and gates releases; what is left
is noted at the end.

`.github/workflows/build.yml` gains a `windows-check` job: it takes the exe
the Linux job already uploads as an artifact, and on a `windows-latest`
runner renders a frame headlessly with `--frame` in three configurations --
the shipping default, `--zoom 4` (which is the separate `unitScale` path in
the resample), and `--highlight 1` (a whole-map pass that deliberately falls
back to the full repaint). Each run must exit 0, must log
`worst frame differed in 0 pixels` from `--selfcheck`, and must produce a
frame in which at least 1 % of sampled pixels differ from the background, so
a blank screen fails rather than passing quietly.

That covers, on the platform it ships to, what nothing covered before:
process start, snapshot load, ini defaults and clamping, view setup, palette
building, both resample paths and the partial repaint's correctness.

Still open:

- ~~It does not gate releases.~~ **Done 2026-09-05.** `Release` is its own
  job with `needs: [build, windows-check]`, taking the binaries from the
  artifact the build job uploads, so a tagged build cannot publish unless the
  Windows check has passed.
- ~~The frame is checked for being a picture, not the right picture.~~
  **Done 2026-09-05.** `tools/read-frame.py` reads the seven segments off
  the BMP, using a `geometry:` line the program now logs to map pattern
  coordinates to screen pixels. `--gen N` renders one exact generation so a
  frame does not depend on the wall clock, and CI renders three that
  `test_clock.c` reports as reading cleanly -- 12:05 PM, 6:00 PM, 12:00 AM --
  and asserts each. Verified on Windows: all three read correctly, AM/PM dot
  and leading 1 included.

  The threshold in that reader is the subtle part. A lit horizontal segment
  measures ~175 and a lit vertical ~75 against a background of 31, because
  the sample rectangles are filled differently, so a midpoint split discards
  every lit vertical. What is uniform is the unlit level, which is just
  background, so the cut sits at 15 % of the range above the dimmest sample;
  the dimmest lit segment clears that by 28 %.
- **First run failed on a quoting bug, fixed 2026-09-06.** A bash
  single-quote escape (`'"'"'`) written inside an already-quoted heredoc
  leaked into the workflow's PowerShell verbatim. The job died parsing the
  script in 11 seconds and never launched the exe, so the check was still
  unproven. Before pushing a change to a `shell: pwsh` block, extract it and
  parse it: PowerShell's own parser will say, and it costs seconds.

      python3 -c "import yaml; ..."   # write each pwsh run: block to a file
      [System.Management.Automation.Language.Parser]::ParseFile(...)

- ~~Still unverified on a runner.~~ **Verified 2026-09-06.** The job runs on
  `windows-latest`: all three configurations render, the partial repaint is
  exact (0 of 786,432 pixels differ, the runner being 1024x768), and the three
  `--gen` frames read back as 12:05 PM, 6:00 PM and 12:00 AM. The frame reader
  transferred to a display it was never tuned for -- segment brightnesses of
  169/72/31 there against 175/75/31 locally, threshold 52 against 53.

## 5. The display lag

**Done 2026-09-05.** 12,800 stays; the documentation was what was wrong.

`native/sweep_lag.c` walks the whole 24-hour cycle at 64-generation
resolution recording what the display reads, then scores every lag from
6,400 to 24,000 against that single pass -- a lag only chooses which of
those readings the wallpaper would have shown, so one sweep answers for all
of them. `tools/lag-progress.sh` watches it (20 to 35 minutes);
`SWEEP_CACHE=<file>` saves the readings so re-scoring is instant.

Result: the best mean available is 29.8 s of each minute correct, at lag
11,584. 12,800 gives 29.6, on a plateau from about 11,300 to 14,300. No
better constant exists, and the worst case cannot be bought either -- the
best worst-case minute over all lags is 4 s, at 8,128, whose mean is 21.4.

What the sweep did find is that `docs/SETUP.md` was wrong twice over. It
claimed 20 to 45 seconds a minute, "least around an hour rollover". The real
range is 3 to 51 s, and the worst minutes are not rollovers: they are every
minute ending in 7, at any hour.

| Last digit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Mean seconds correct | 35 | 12 | 43 | 23 | 13 | 44 | 43 | **3** | 47 | 33 |

A digit reads correctly only once every segment that should be dark has
drained, so what matters is how many segments the previous digit lit that
this one does not. 7 follows 6 and must clear d, e, f and g while lighting
only b: three seconds. 8 follows 7 and clears nothing: forty-seven. Both
documents now say this.

Two things about the sweep worth keeping:

- **Do not wrap the cycle.** The first version scored the last minutes of
  the day by taking generations past 24 hours modulo the cycle. That is
  wrong: generation 0 was hand-set to 12:00 rather than arrived at from
  11:59, so the start of the pattern is not a continuation of its end. The
  wallpaper does not wrap either -- it loads the last snapshot and runs the
  machine on past 24 hours. The sweep now samples 30,000 generations past
  the end. Before the fix minutes 1438 and 1439 scored 0 and every
  worst-case figure was meaningless.
- **The progress ETA was half the truth.** `tools/lag-progress.sh` derives
  it from the measured rate, but the rate decays as the memo grows and the
  collector starts firing: it read 17 minutes on a run that took 34.

## 7. Memory

**Reduced 2026-09-06, measured over 40 simulated minutes: 122 MB peak to
74 MB, and about 40 % faster.**

Where it went, before: node array 117 MB, node hash 34 MB, tile pixels 34 MB,
tile keys 8 MB.

- **The tile cache was sized for ten times what it uses.** 524,288 tile slots
  and a million key slots, holding a measured peak of 50,557 tiles. Now
  131,072 and 262,144 -- still 2.5x the observed peak, and no flush occurred
  in 40 simulated minutes.
- **The node array doubled and was never collected.** `cap_` doubles at 2M
  nodes; collection was set to trigger at 3M, which the machine takes over 40
  minutes to reach. So it grew to 117 MB and stayed there. Collecting at 1.5M
  keeps it an order of magnitude smaller.
- **`hl_gc` reallocated at the same capacity**, so even when it did run the
  memory never came back. It now sizes to twice the surviving count.

The assumption worth killing: collecting more often was avoided as a
memory-for-CPU trade. It is not a trade. Measured, alternating the two
settings over three runs, collecting at 1.5M against 3M ran 40 minutes of
work in 48.8, 50.6 and 55.8 s against 87.1, 77.2 and 71.0 s. A collected
table is about 28 MB and stays in cache; a four-million-node one is 117 MB
and thrashes it. The collection pays for itself several times over.

| collect at | peak tables | collections | 40 min in | peak RSS |
|---|---|---|---|---|
| 3,000,000 (old) | 161.5 MB | 0 | 71-87 s | 122 MB |
| 1,900,000 | 86.0 MB | 4 | 56 s | 92 MB |
| **1,500,000** | **86.0 MB** | 8 | **49-56 s** | **74 MB** |
| 1,000,000 | 86.0 MB | 43 | 59 s | 61 MB |

Not done: the node struct is 28 bytes (`a, b, c, d, pop, res` as int32 plus
level and resJ), and packing it would save proportionally, but that is
invasive for a table now an order of magnitude smaller than it was.

## 6. Smaller

- **`afterglow` costs more than it looks.** It is a whole-map pass, so
  `render()` takes the whole-picture path whenever it is on and the partial
  repaint is disabled: measured on the default view, resample 3.3 ms a frame
  instead of 1.2. Restricting it would mean tracking a decaying union rather
  than the live set, since a pixel must keep being touched after it empties.
  Documented in `docs/SETUP.md` rather than fixed.
- **Executable size.** 16.6 MB, nearly all snapshot data
  (`native/snapshots_data.c` is 57.7 MB of source, from 16 MB of
  `snapshots/*.rle.gz`). 144 snapshots at 10-minute spacing buy a 1–2 s
  start. 30-minute spacing would give 48 snapshots and roughly a 6 MB exe
  for a 4–6 s start. A trade, not an improvement — worth knowing the option
  exists.
- **Test coverage of the cycle.** `test_clock` samples 12 minutes.
  `native/sweep_lag.c` now covers all 1,440 -- it reads the display at every
  64th generation of the day and knows which minute each reading shows -- but
  it is a 20-to-35-minute offline tool, not a test, and nothing asserts on
  its output. Turning it into a pre-release gate would mostly mean deciding
  what to assert, given that a minute ending in 7 is legitimately correct for
  only three seconds.
- ~~No test for the settings layer.~~ **Done 2026-09-06.** The config struct,
  `apply_setting`, `clamp_settings`, `parse_hex_bgr` and `preset_colors` moved
  out of `main.c` into `native/config.c`, which builds without `windows.h`, so
  `native/test_config.c` exercises them on Linux in CI: every enumerated key,
  the BGR colour handling, that clamping rejects out-of-range values and
  leaves legal ones alone, and that an unknown key is ignored.

## Ranking

1. ~~Span-restricted resample (item 1)~~ — implemented; the Windows measurement is what is left of it.
2. ~~Windows CI (item 4)~~ — done; the release-gating split is what is left of it.
3. ~~Lag sweep (item 5)~~ — done; 12,800 confirmed, the documentation corrected.

Items 2, 3 and 6 are real but none of them is on the critical path of
anything.

## Conventions worth not rediscovering

- Build and test commands are in `docs/HOW-IT-WORKS.md` under "Building".
  Both tests run on Linux with plain `gcc`; only the shipped binary needs Zig.
- `native/snapshots_data.c` and `native/life-clock.exe` are generated and
  gitignored. `tools/gen-snapshot-data.py` regenerates the first from
  `snapshots/*.rle.gz`.
- `DISPLAY_LAG` is duplicated between `native/main.c` and
  `native/test_clock.c` and the test comment says so. Change both.
- `RESEARCH.md` is the survey that chose this design. It is history, not a
  plan — options 2 through 5 in it were considered and rejected.
