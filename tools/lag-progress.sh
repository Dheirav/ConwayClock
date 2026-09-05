#!/bin/sh
# Progress for native/sweep_lag, which writes "<done> <total> <elapsed>" to a
# file as it goes. The ETA comes from the rate measured so far, never a guess.
#   tools/lag-progress.sh [file] [--watch]
F="${1:-/tmp/lag-sweep.progress}"
show() {
  [ -f "$F" ] || { echo "no progress yet ($F)"; return; }
  read -r done total secs < "$F"
  [ -n "$done" ] || { echo "no progress yet"; return; }
  pct=$(awk "BEGIN{printf \"%.1f\", 100*$done/$total}")
  rate=$(awk "BEGIN{printf \"%.0f\", ($secs>0 ? $done/$secs : 0)}")
  if [ "$rate" -gt 0 ] 2>/dev/null; then
    eta=$(awk "BEGIN{printf \"%.0f\", ($total-$done)/($done/$secs)}")
    printf '%s/%s samples (%s%%)  elapsed %.0fs  %s/s  eta %sm%ss\n' \
      "$done" "$total" "$pct" "$secs" "$rate" "$((eta/60))" "$((eta%60))"
  else
    printf '%s/%s samples (%s%%)  elapsed %.0fs  rate not yet measurable\n' "$done" "$total" "$pct" "$secs"
  fi
}
if [ "$2" = "--watch" ] || [ "$1" = "--watch" ]; then
  [ "$1" = "--watch" ] && F=/tmp/lag-sweep.progress
  while :; do printf '\r%-90s' "$(show)"; sleep 2; done
else
  show
fi
