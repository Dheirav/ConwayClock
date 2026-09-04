# Setup and settings

## Requirements

Windows 10 or 11 (tested on Windows 11 build 26200). One 16 MB executable,
no installer, no runtime, no admin rights.

## Install

Copy `native/` to a folder of your choice, for example
`C:\Users\you\Pictures\life-clock\native\`. The program writes three files
next to itself: `life-clock.ini` (settings), `life-clock.log` (today's log)
and `life-clock.prev.log` (yesterday's).

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

## Tray icon

While running, an amber clock icon sits in the notification area (it may be
in the overflow behind the ^ arrow). Right-click it for: Pause / Resume,
Watch full screen, Open settings, Open log, Quit.

## Watch mode

To actually watch the machine, gliders and all:

```
life-clock.exe --fullscreen 1
```

or "Watch full screen" from the tray menu. It opens a normal window covering
the screen at 1/4 zoom, centred on the digits, synced like the wallpaper.
Esc, Q or a click closes it; + and - (or up/down) zoom between 1/16 and
1/1; the arrow keys and Page Up/Down pan. It runs alongside the wallpaper,
which pauses while the window covers the desktop.

## Start at login

Win+R, type `shell:startup`, Enter, and put a shortcut to `life-clock.exe`
in the folder that opens.

## Settings: `life-clock.ini`

Created with comments on first run (a copy is in
`native/life-clock.sample.ini`). Edit and save; the wallpaper picks up the
change within two seconds without a restart. Every key can also be given
on the command line as `--key value`, which overrides the file.

| Key | Default | Meaning |
|---|---|---|
| `fps` | 6 | Frames per second: 3, 6, 12 or 24. Generations per frame = 192 / fps. The one setting that changes CPU use: roughly 2 %, 4 %, 8 %, 16 % of one core |
| `battery_fps` | 3 | Frame rate while on battery: 1, 3, 6, 12 or 24 |
| `view` | whole | `whole` shows the machine; `display` shows only the 7-segment display |
| `size` | 1.0 | Relative to the largest fit: 0.5 is half size, above 1 crops |
| `hpos` | 0.5 | Where the digits' centre sits horizontally, as a fraction of the screen width |
| `vpos` | 0.5 | Same vertically. At 0.5 the top of the machine is cropped; 0.78 shows all of it |
| `monitor` | 0 | Monitor index, 0 = primary. Multi-monitor is untested |
| `palette` | amber | `amber`, `green`, `white`, `blue` or `red` |
| `bg`, `cells` | | Your own background and cell colours as RRGGBB; override the palette |
| `cells2` | none | If set, the densest areas fade from `cells` to this colour |
| `gain` | 40 | Brightness of a single live cell in a zoomed-out pixel, 5 to 120 |
| `pm` | dot | AM/PM. The machine has a box that is an outline in the morning and filled in the afternoon, next to a static "PM" label. `dot`: blank both, draw a filled dot beside the digits when the box says PM. `text`: keep the box, write AM or PM beside it. `machine`: show the corner as drawn. `hide`: blank it |
| `colon` | pulse | The pattern's colon is two discs of still-life blocks. `pulse`: replace them with discs of pulsars so the dots breathe under Life's rules. `machine`: keep the blocks. `hide`: remove them |
| `zoom` | auto | `auto` fits the view to the screen (1/8 for the whole machine). `8`, `4`, `2` or `1` fix the cells per pixel, centred on the digits with `hpos`/`vpos`; 4 shows individual gliders. Watch mode defaults to 4 |
| `status` | 0 | Small line in the corner with generation, target and engine statistics |
| `attach` | 7 | How the window is attached to the desktop; see troubleshooting |

Only `fps` and `battery_fps` affect resource use. Colours and layout are
applied once per change.

## What the display does over a minute

This is how the machine behaves, not a defect. After its counter ticks, a
segment switching on fills with gliders within a few hundred generations,
but a segment switching off keeps the gliders already travelling along it
until they drain out, up to about a minute of machine time. So each digit
change begins 40 to 60 seconds into the real minute and finishes 0 to 37
seconds into the next, and a digit can briefly read as something else on
the way (a 1 becoming a 2 passes through a 7). The program runs the machine
11,450 generations ahead of its counter so the display is never early:
stable digits are always the current minute or, during a redraw, the
previous one.

## Pausing

The program stops stepping, and uses no CPU, whenever the desktop is fully
covered by windows, the session is locked or the display is off. When the
desktop becomes visible again it catches up in one jump. On battery it
drops to `battery_fps`.

## Troubleshooting

**Nothing appears behind the icons.** Open `life-clock.log`. The lines
`attach ...`, `synced ...` and `paused`/`resumed` say what it decided. If it
attached but nothing shows, your Windows build may lay out the desktop
differently; try `attach = 1` (child of Progman, the Windows 11 24H2
layout), then `attach = 2` (child of the wallpaper WorkerW, the classic
layout). The default `attach = 7` (inside the shell view) is what works on
build 26200. Details in [How it works](HOW-IT-WORKS.md).

**It shows the wrong time.** The machine is synced to the local clock at
start and resynced if it drifts more than a minute; a time-zone or clock
change is picked up within a minute. Check `synced to gen ...` in the log.

**It seems frozen.** Look for `paused` in the log: something covers the
desktop, including a maximised window. Show the desktop.

**The PM dot is wrong.** It is read from the machine's own indicator, which
switches within ten minutes of noon and midnight. If it disagrees with the
clock, the sync is off; see the log.

**Explorer restarted.** The window is recreated and re-attached
automatically within a second.
