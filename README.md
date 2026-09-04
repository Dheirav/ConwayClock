# Life Clock wallpaper

A digital clock built *inside* Conway's Game of Life, running in real time
behind your desktop icons, as a single small Windows program.

The pattern is the one from the Code Golf Stack Exchange challenge "Build a
digital clock in Conway's Game of Life", built by the user **dim** in 2017
(`clock.rle`). It is a real Life machine: a glider-gun timebase at the top,
clock distribution and counters, latches and lookup tables in the middle,
and a 7-segment display at the bottom whose segments are bundles of glider
streams. Nothing about Life's rules is changed; the machine *computes* the
time. It advances one displayed minute every 11,520 generations, so keeping
it in step with the wall clock means 192 generations per second.

| Pattern | 10,016 x 6,796 cells, about 245,000 alive at generation 0, 340,000-400,000 running |
|---|---|
| One displayed minute | 11,520 generations |
| Real time | 192 generations per second |
| Full cycle | 24 hours = 16,588,800 generations: the PM indicator is lit for the first 12 hours of the cycle and dark for the second |

## Running it

`native/life-clock.exe`. Double-click it: no window appears, it goes
straight behind the desktop icons, syncs to the current time in a second or
two, and runs. Stop it with `life-clock.exe --quit`. To start it at login,
put a shortcut to it in the folder that `shell:startup` opens (Win+R,
`shell:startup`).

Everything is in one 16 MB executable: the Hashlife engine in C, and 144
precomputed states of the machine (one per 10 minutes of its 24-hour
cycle) so it can start near the current minute instead of simulating hours.

### Settings: `life-clock.ini`

Created next to the executable on first run, with comments (a copy is in
`native/life-clock.sample.ini`). Edit and save; the wallpaper reloads it
within 2 seconds, no restart. None of these cost
anything while running: colours and layout are applied once per change.

| Key | Default | Meaning |
|---|---|---|
| `fps` | 6 | 3, 6, 12 or 24 frames per second; generations per frame = 192 / fps. Lower = less CPU |
| `battery_fps` | 3 | Frame rate while on battery: 1, 3, 6, 12 or 24 |
| `view` | whole | `whole` machine, or `display` for just the 7-segment display |
| `size` | 1.0 | Relative to the largest fit; 0.5 = half, above 1 crops |
| `hpos`, `vpos` | 0.5, 0.5 | Where the digits' centre sits, as a fraction of the screen width / height. With `vpos = 0.5` the top of the machine is cropped; 0.78 shows all of it |
| `monitor` | 0 | Monitor index, 0 = primary (multi-monitor untested) |
| `palette` | amber | `amber`, `green`, `white`, `blue`, `red`; or set colours yourself |
| `bg`, `cells` | | Background and cell colour, RRGGBB; override the palette |
| `cells2` | none | If set, a two-tone ramp: the densest areas fade from `cells` to this colour |
| `gain` | 40 | Brightness of one live cell in a zoomed-out pixel, 5-120 |
| `pm` | dot | The machine has an AM/PM box (outline in the morning, filled in the afternoon) next to a static "PM" label. `dot` blanks both and draws a filled dot beside the digits when the box says PM, nothing for AM; `text` keeps the box and writes AM/PM; `machine` shows the corner as drawn; `hide` blanks it |
| `colon` | pulse | The pattern's colon is two discs of still-life blocks that render flat. `pulse` replaces them, at load, with discs of pulsars (period-3 oscillators) so the dots breathe under Life's rules; `machine` keeps the blocks; `hide` removes them. Measured cost: none (0.69 vs 0.72 ms per frame, fewer cells) |
| `status` | 0 | Small stats line in the corner |

The same names work as command-line flags (`--fps 3`, `--palette green`)
and override the file. `--frame out.bmp` renders one frame headlessly and
exits, for checking a setting.

The program writes `life-clock.log` next to itself and rotates it daily: at
the first entry of a new day it becomes `life-clock.prev.log`, replacing the
old one, so at most two days (a few hundred KB) are ever kept.

### What it costs

Profiled on this machine (RTX 4060 laptop, 1080p at 125 %, Windows 11 build
26200) with the desktop visible, process CPU sampled over 42 s:

| Setting | CPU, one core | Per frame | Memory |
|---|---|---|---|
| 6 fps (default) | 3.8-4.2 % | render 0.6-1.0 ms, resample 3.2-3.5 ms, blit 0.9-1.0 ms | 97 MB working set |
| 3 fps | about half | same per frame | same |
| Desktop covered, locked or display off | 0 % | no frames | same |

GPU: none. The program draws on the CPU into a bitmap that Windows
composites exactly as it composites a static wallpaper. For comparison the
same clock as a Lively web wallpaper cost about 350 MB and 10-20 % of a
core with a busy GPU process, and a hardware-decoded video wallpaper runs
several watts above idle.

### What the display looks like over a minute

A property of the machine, not the wallpaper. After the counter ticks, a
segment switching on fills with gliders within a few hundred generations,
but a segment switching off keeps the gliders already travelling along it
until they drain, up to about a minute of machine time. So each digit
change begins 40-60 seconds into the real minute and finishes 0-37 seconds
into the next, and a digit can briefly read as something else on the way (a
1 becoming a 2 passes through a 7). The program runs the machine 11,450
generations ahead of its counter so the display is never *early*: stable
digits are always the current minute or, during a redraw, the previous one.

## How it works

**Engine.** `native/hashlife.c` is Hashlife (Gosper's memoised quadtree)
on flat arrays: 28-byte nodes with the result cached inline, an
open-addressing node table, a tile cache for rendering, and periodic
compaction. Warm, a real-time step of the whole machine takes 0.5-1 ms.
`native/test_hl.c` checks it against a naive simulator and prints a
population trace that matches the JavaScript engine used to generate the
snapshots, byte for byte in the bitmap.

**Sync.** Generation 0 shows 12:00 PM, so minute M of the cycle is M
minutes after noon. Target generation = M x 11,520 + 11,450 + seconds x 192.
On start the nearest earlier snapshot is loaded and advanced to the target;
if the clock jumps or the program falls far behind, it resyncs from a
snapshot. While the desktop is covered it does nothing and catches up in one
jump when the desktop is visible again.

**Behind the icons on Windows 11 24H2/25H2.** The classic WorkerW trick no
longer applies, and neither does parenting under Progman (its child windows
are not composed on build 26200). What works is parenting inside
`SHELLDLL_DefView` at the bottom of its children, beneath the icon list.
DefView is a layered window composed with per-pixel alpha, so the pixels
must be opaque. `attach = N` in the ini selects other strategies (1 Progman
child, 2/3 WorkerW child, 5 bottom-most top-level; 7 is the default) for
other Windows builds; a watchdog re-attaches if Explorer restarts.

**Pausing.** Once a second the program walks the top-level windows in
z-order and subtracts each visible, uncloaked, non-minimised, opaque one
from the screen until it reaches Progman; if nothing is left, stepping
stops. Click-through overlays and translucent windows are ignored. Session
lock and display-off come from Windows notifications; on battery it drops to
`battery_fps`.

## Building

From WSL with Zig, no admin, no Windows toolchain:

```bash
Z=~/.local/opt/zig-x86_64-linux-0.14.1/zig
cd native && $Z cc -target x86_64-windows-gnu -O2 -o life-clock.exe \
  main.c hashlife.c inflate.c snapshots_data.c \
  -lgdi32 -luser32 -ldwmapi -lwtsapi32 -lshell32 -Wl,--subsystem,windows
```

Regenerating the snapshots (needs Node): `tools/gensnapshots.js` with
`START`/`END` minutes writes `snapshots/m*.rle.gz`, about 6 s each;
`tools/snapshot-progress.sh --watch` shows progress and ETA;
`tools/gen-snapshot-data.py` packs them into `native/snapshots_data.c`.
`tools/hashlife.js` is the JavaScript engine those tools use and
`tools/reader.js` reads the 7-segment display from a universe.

## Files

- `native/life-clock.exe`, `native/life-clock.ini`, `native/life-clock.log`
- `native/main.c`, `hashlife.c/h`, `inflate.c/h`, `snapshots_data.c`, `test_hl.c`
- `clock.rle`: the pattern (from the anonymous gist linked from the answer)
- `snapshots/`: 144 gzip'd states, one per 10 minutes
- `tools/`: snapshot generation and the display reader
- `RESEARCH.md`: the survey of hosts, engines and precomputation that led here

## Credits

Pattern: dim, answer to "Build a digital clock in Conway's Game of Life" on
codegolf.stackexchange.com (2017), CC BY-SA. Everything else was written
for this wallpaper.
