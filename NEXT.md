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

Runtime figures below (4 % of one core, 97 MB, the per-frame split) are the
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
  drops from ~30 ms/s to ~18 ms/s. The process CPU figure in
  `docs/HOW-IT-WORKS.md` has not been re-measured over a long run.
- **At `zoom = 4` it buys nothing**: 2.31 ms against 2.40 ms. The watch view
  is a dense crop with little empty space. Expected, and the right way round.
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

`pick_monitor` (`native/main.c:645`) selects one display by index; the
others keep the plain wallpaper. `docs/SETUP.md` marks `monitor` untested.

Two separable pieces:

- Verify the index actually works on a second display. Cheap, needs
  hardware.
- Decide what spanning means: one machine stretched across the virtual
  desktop, or one instance per monitor. `attach_to_desktop`
  (`native/main.c:275`) parents into a single `SHELLDLL_DefView`, so the
  stretched version is window geometry rather than an architecture change.
  Per-monitor instances collide with the single-instance mutex in `main`.

## 3. The screensaver preview is blank

`native/main.c:664` — `if (scrPreview) return 0;`.

Windows passes `/p <hwnd>` and expects the screensaver to draw into that
child window. The program parses `/p`, ignores the handle that follows, and
exits, so the small monitor in the screensaver dialog stays black.

Small, visible in a dialog people actually open, and the machinery to
render into an arbitrary HWND already exists — `--frame` renders headless
into a memory DIB (`native/main.c:713`) and could blit into the preview
handle at a low frame rate.

## 4. CI and the Windows code

**Done 2026-09-05**, except for one gap noted at the end.

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

- **It does not gate releases.** The `Release` step lives in the `build`
  job, which finishes before `windows-check` runs, so a tagged build
  publishes whether or not the Windows check passes. Closing that means
  splitting `Release` into its own job with
  `needs: [build, windows-check]` -- straightforward, but it touches the
  publishing path, so it is worth doing deliberately rather than as a
  side effect.
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
- **Unverified on a runner.** The job is written but has never executed;
  in particular nobody has confirmed that a `windows-latest` runner gives
  the process a display GDI is happy with. The first push will say.

## 5. The documented on-screen window does not hold near the wrap

`docs/SETUP.md` says the right time is on screen "for 20 to 45 seconds of
each minute, least around an hour rollover." The 2026-09-05 `test_clock`
run measured a wider spread than that at both ends:

| Time | Correct for |
|---|---|
| 12:05 | 40 s (seconds 12–52) |
| 12:10, 14:10 | 24 s |
| 13:00 | 16 s |
| **00:01** | **8 s** (seconds 24–32) |

So the floor is 8 s, not 20 s, and the outlier is 00:01 — the minute after
the 12-hour wrap — not 00:00, and not an hour rollover. Every test passed;
this is the machine's redraw behaviour, not a fault. But it is the one
number in `SETUP.md` a user would check their clock against.

Two ways out:

- Widen the documented range to what the test prints. Honest, five minutes.
- More interesting: `DISPLAY_LAG = 12800` (`native/main.c`, mirrored in
  `native/test_clock.c:18`) came from sampling *some* minutes across the
  cycle. If the wrap minutes settle in a different window, the single
  centred constant is not centred there, and a per-region lag would buy
  back visible seconds. `test_clock.c` already computes the correct-seconds
  range per minute, so sweeping lag values across all 1,440 minutes is a
  loop around code that exists — an offline job, run once, that either
  finds a better constant or proves 12,800 is right and settles the
  documentation.

## 6. Smaller

- **Executable size.** 16.6 MB, nearly all snapshot data
  (`native/snapshots_data.c` is 57.7 MB of source, from 16 MB of
  `snapshots/*.rle.gz`). 144 snapshots at 10-minute spacing buy a 1–2 s
  start. 30-minute spacing would give 48 snapshots and roughly a 6 MB exe
  for a 4–6 s start. A trade, not an improvement — worth knowing the option
  exists.
- **Test coverage of the cycle.** `test_clock` samples 12 minutes. A full
  1,440-minute sweep is too slow for CI but fine as a pre-release job, and
  it is the same loop item 5 needs.
- **No test for the settings layer.** `apply_setting` and `clamp_settings`
  (`native/main.c:506`, `:540`) parse every ini key and are exercised only
  by hand.

## Ranking

1. ~~Span-restricted resample (item 1)~~ — implemented; the Windows measurement is what is left of it.
2. ~~Windows CI (item 4)~~ — done; the release-gating split is what is left of it.
3. Lag sweep (item 5) — now the largest open item, and it either improves the clock or settles the docs.

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
