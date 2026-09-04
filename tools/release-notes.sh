#!/usr/bin/env bash
# Builds the GitHub release description for a tag: what the release is (the
# body of the tagged commit), how to install it, what changed since the
# previous tag, and the checksums. Used by CI and runnable by hand:
#   tools/release-notes.sh v1.6 [path/to/SHA256SUMS.txt]
set -euo pipefail
TAG="$1"; SUMS="${2:-}"
REPO="${GITHUB_REPOSITORY:-Dheirav/ConwayClock}"
PREV="$(git describe --tags --abbrev=0 "$TAG^" 2>/dev/null || true)"

# The tagged commit's body describes the release; fall back to its subject.
BODY="$(git log -1 --pretty=%b "$TAG" | sed -e :a -e '/^\n*$/{$d;N;};/\n$/ba')"
[ -n "$BODY" ] || BODY="$(git log -1 --pretty=%s "$TAG")"

cat <<MD
$BODY

## Install

Download **life-clock.exe** and double-click it. It syncs to the current time in a
second or two and appears behind your desktop icons.

- \`life-clock.exe --setup\` installs it into \`%LOCALAPPDATA%\\LifeClock\` with Start-menu
  and startup shortcuts and an entry in Windows' Installed apps.
- Right-click the tray icon for settings, a full-screen watch view, and pause.
- **life-clock.scr** is the same program as a screensaver: right-click it and choose Install.

[Setup and settings](https://github.com/$REPO/blob/$TAG/docs/SETUP.md) ·
[How it works](https://github.com/$REPO/blob/$TAG/docs/HOW-IT-WORKS.md)
MD

if [ -n "$PREV" ]; then
  printf '\n## Changes since %s\n\n' "$PREV"
  git log --no-merges --pretty='- %s' "$PREV..$TAG"
fi

if [ -n "$SUMS" ] && [ -f "$SUMS" ]; then
  printf '\n## Checksums\n\n```\n'
  cat "$SUMS"
  printf '```\n'
fi

cat <<'MD'

Windows will warn that the file is unsigned, because it is: there is no code-signing
certificate for this project. Choose **More info**, then **Run anyway**, or check the
SHA-256 above against the file you downloaded.
MD
