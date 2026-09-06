# Setup and settings

## Requirements

Windows 10 or 11 (tested on Windows 11 build 26200). One 16 MB executable,
no installer, no runtime, no admin rights.

## Install

Run `life-clock.exe --setup` (or Install on this PC in the tray menu). The
setup window offers to copy it into `%LOCALAPPDATA%\LifeClock`, add Start
menu and startup shortcuts, and optionally set it as the screensaver. It
registers an uninstall entry, so it appears in Settings > Apps > Installed
apps like any other program, and Uninstall in the same window (or
`life-clock.exe --uninstall`) removes everything it added.

`--install` and `--uninstall` do the same silently, for scripts.

Installing is optional: the executable is self-contained and runs from
wherever you put it. It writes three files next to itself: `life-clock.ini`
(settings), `life-clock.log` (today's log) and `life-clock.prev.log`
(yesterday's).

## Run

Double-click `life-clock.exe`. No window or console appears. Within a
second or two it loads the precomputed state nearest the current minute,
advances to the current second, and starts drawing behind the desktop
icons. Show the desktop (Win+D) to see it.

Stop it from any terminal, or from a second shortcut:

```
life-clock.exe --quit
```

Only one instance runs at a time; starting a second one exits immediately.

## Screensaver

The same program is a Windows screensaver. Copy `life-clock.exe` to
`life-clock.scr` (the release has it ready), right-click the `.scr` and
choose Install, or put it in `C:\Windows\System32` and pick it in
Settings, Personalisation, Lock screen, Screen saver settings. It runs the
watch view at 1/4 zoom, touring the machine, and exits on mouse movement
or a key. The dialog's small preview monitor shows the machine too, synced
to the current time like everything else. "Settings"
in that dialog opens the settings window.

## Tray icon

While running, an amber clock icon sits in the notification area (it may be
in the overflow behind the ^ arrow). Right-click it for: Pause / Resume,
Watch full screen, Settings, Open log, Start with Windows, Install on
this PC (until it is installed), Quit.

## Watch mode

To actually watch the machine, gliders and all:

```
life-clock.exe --fullscreen 1
```

or "Watch full screen" from the tray menu. It opens a normal window covering
the screen at 1/4 zoom, centred on the digits, synced like the wallpaper.
Esc, Q or a click closes it; + and - (or up/down) zoom between 1/16 and
1/1; the arrow keys and Page Up/Down pan; T starts or stops the tour. It runs alongside the wallpaper,
which pauses while the window covers the desktop.

## Start at login

Tick "Start with Windows" in the tray menu, or run:

```
life-clock.exe --install-startup
```

Either writes a shortcut into your Startup folder; `--uninstall-startup` or
unticking removes it.

## Settings

Right-click the tray icon and choose Settings, or run
`life-clock.exe --settings`. A window with a control for every setting:
palette and colour pickers, sliders for brightness, size and position,
drop-downs for frame rate, view, zoom, monitor, AM/PM, colon and tour, checkboxes
for highlight, status line and start with Windows. Every change is written
to `life-clock.ini` at once and the running wallpaper picks it up within
two seconds. Shift-click a colour button to set it to `none`.

The file itself is plain text with comments (a copy is in
`native/life-clock.sample.ini`) and can be edited directly; the wallpaper
reloads it the same way. Every key can also be given on the command line
as `--key value`, which overrides the file.

| Key | Default | Meaning |
|---|---|---|
| `fps` | 6 | Frames per second: 3, 6, 12 or 24. Generations per frame = 192 / fps. The one setting that changes CPU use: roughly 2 %, 4 %, 8 %, 16 % of one core |
| `battery_fps` | 3 | Frame rate while on battery: 1, 3, 6, 12 or 24 |
| `view` | whole | `whole` shows the machine; `display` shows only the 7-segment display |
| `size` | 1.0 | Relative to the largest fit: 0.5 is half size, above 1 crops |
| `hpos` | 0.5 | Where the digits' centre sits horizontally, as a fraction of the screen width |
| `vpos` | 0.5 | Same vertically. At 0.5 the top of the machine is cropped; 0.78 shows all of it |
| `monitor` | 0 | Monitor index, 0 = primary. Tested on two monitors; the wallpaper draws on the one chosen, and the others keep the Windows wallpaper |
| `theme` | off | Day/night switching. `off` uses the single palette below. `clock` switches at `day_start` and `night_start`. `system` follows the Windows light/dark app theme |
| `day_start`, `night_start` | 7, 19 | Hours at which day and night begin, when `theme = clock` |
| `day_palette`, `night_palette` | white, amber | A preset name or an RRGGBB colour for each half of the day |
| `day_gain`, `night_gain` | 40, 22 | Brightness for each; night is dimmer by default |
| `fade` | 3 | Seconds the change from one to the other takes. 0 is instant |
| `palette` | amber | Used when `theme = off`: `amber`, `green`, `white`, `blue` or `red` |
| `bg`, `cells` | | Your own background and cell colours as RRGGBB; override the palette |
| `cells2` | none | If set, the densest areas fade from `cells` to this colour |
| `gain` | 40 | Brightness of a single live cell in a zoomed-out pixel, 5 to 120 |
| `pm` | dot | AM/PM. The machine has a box that is an outline in the morning and filled in the afternoon, next to a static "PM" label. `dot`: blank both, draw a filled dot beside the digits when the box says PM. `text`: keep the box, write AM or PM beside it. `machine`: show the corner as drawn. `hide`: blank it |
| `colon` | pulse | The pattern's colon is two discs of still-life blocks. `pulse`: replace them with discs of pulsars so the dots breathe under Life's rules. `machine`: keep the blocks. `hide`: remove them |
| `highlight` | 0 | 1 colours cells that changed since the last frame in the `hot` colour (default c8e9ff), so the working parts of the machine stand out from the static hardware |
| `tour` | auto | In watch mode and the screensaver, pan slowly around the machine: display, digits, colon, lookup tables, clock distribution, timebase, counters. `auto` is on for the screensaver and off for watch mode; T toggles it in the window |
| `afterglow` | 0 | 0 is off; 0.5 to 0.9 leaves fading trails behind moving cells. Not cheap: it is a pass over every pixel, and because of that it turns off the partial repaint, so the resample costs about 3.3 ms a frame instead of 1.2. Most useful at zoom 4 or closer |
| `zoom` | auto | `auto` fits the view to the screen (1/8 for the whole machine). `8`, `4`, `2` or `1` fix the cells per pixel, centred on the digits with `hpos`/`vpos`; 4 shows individual gliders. Watch mode defaults to 4 |
| `status` | 0 | Small line in the corner with generation, target and engine statistics |
| `attach` | 7 | How the window is attached to the desktop; see troubleshooting |

Only `fps` and `battery_fps` affect resource use. Colours and layout are
applied once per change.

## What the display does over a minute

This is how the machine behaves, not a defect. After its counter ticks, a
segment switching on fills with gliders within a few hundred generations,
but a segment switching off keeps the gliders already travelling along it
until they drain out. Measured across the cycle, the digits for a minute
settle between 1.23 and 1.63 minutes after that minute's own generation and
begin changing again between 1.68 and 1.99 minutes, so the machine spends
about half of every minute redrawing, and a digit can briefly read as
something else on the way (a 1 becoming a 2 passes through a 7).

The program runs the machine 12,800 generations ahead of its counter.
`native/sweep_lag.c` walked the whole 24-hour cycle at 64-generation
resolution and scored every lag from 6,400 to 24,000: the best mean
available is 29.8 seconds of each minute correct, at lag 11,584, and 12,800
gives 29.6 on a plateau running from about 11,300 to 14,300. There is no
better constant to be had.

How long the right time is actually on screen varies far more than that mean
suggests, and it depends almost entirely on the last digit of the minute:

| Last digit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| Mean seconds correct | 35 | 12 | 43 | 23 | 13 | 44 | 43 | **3** | 47 | 33 |

A digit reads correctly only once every segment that should be dark has
drained. 7 follows 6 and has to clear four segments (d, e, f and g) while
lighting only b, so a minute ending in 7 is right for about three seconds;
8 follows 7 and clears nothing, so it is right for forty-seven. Over all
1,440 minutes, 10 % are correct for under ten seconds and 48 % for forty or
more, the best being 12:00 at fifty-one. `native/test_clock.c` checks a
sample of this on every push.

## Pausing

The program stops stepping, and uses no CPU, whenever the desktop is fully
covered by windows, the session is locked or the display is off. When the
desktop becomes visible again it catches up in one jump. On battery it
drops to `battery_fps`.

## Troubleshooting

**Nothing appears behind the icons.** The program checks for itself: a
few seconds after the desktop is first visible it compares a patch of the
screen with what it drew (`screen check ... N% of the patch matches` in
`life-clock.log`) and, if the picture is not there, tries the other
attachment strategies in turn (7 inside the shell view, 1 child of Progman,
2 and 3 child of the wallpaper WorkerW, 5 bottom-most top-level window),
logging each. If none verifies, look at the log's `attach` lines and the
notes in [How it works](HOW-IT-WORKS.md); `attach = N` in the ini forces
one.

**It shows the wrong time.** The machine is synced to the local clock at
start and resynced if it drifts more than a minute; a time-zone or clock
change is picked up within a minute. Check `synced to gen ...` in the log.

**It seems frozen.** Look for `paused` in the log: something covers the
desktop, including a maximised window. Show the desktop.

**The PM dot is wrong.** It is read from the machine's own indicator, which
switches within ten minutes of noon and midnight. If it disagrees with the
clock, the sync is off; see the log.

**Resolution, scaling or monitor changed.** Handled: the window and
bitmap are rebuilt at the new size within a frame.

**Explorer restarted.** The window is recreated and re-attached
automatically within a second.
