#!/usr/bin/env bash
# Shows snapshot generation progress: done/total, elapsed, and an ETA from the
# measured per-snapshot rate. --watch refreshes every 10 s.
P="$(dirname "$0")/../snapshots/progress.json"
show() {
  python3 - "$P" <<'PY'
import json, sys, time
try: d = json.load(open(sys.argv[1]))
except Exception as e: print('no progress yet'); sys.exit()
done, total = d['done'], d['total']; el = (time.time()*1000 - d['started'])/1000
secs = d['seconds']; rate = sum(secs[1:])/len(secs[1:]) if len(secs) > 1 else None
eta = f"ETA {int(rate*(total-done)//60)} min {int(rate*(total-done)%60)} s (from {len(secs)-1} measured jumps, avg {rate:.0f} s each)" if rate else "ETA: no rate yet"
print(f"{done}/{total} snapshots  elapsed {int(el//60)} min {int(el%60)} s  {eta}")
print(d['note'])
PY
}
if [ "$1" = "--watch" ]; then while true; do clear; show; sleep 10; done; else show; fi
