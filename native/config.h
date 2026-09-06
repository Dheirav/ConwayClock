#ifndef CONFIG_H
#define CONFIG_H
// Every ini key and command-line setting, parsed and clamped. Split out of
// main.c so it can be built and tested without windows.h -- native/test_config.c
// exercises it on Linux in CI, which is the only coverage this layer has.
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>          // DWORD, so the field types match main.c exactly
#else
typedef uint32_t DWORD;       // the Linux test build needs no Windows headers
#endif
struct LcConfig { int fps, batteryFps, view, gain, status, attach, monitor, pmMode, colonMode, zoom, fullscreen, screensaver, tour, highlight, frames, frameStep;
  int theme, dayStart, nightStart, dayGain, nightGain; double fadeSec; char dayPalette[16], nightPalette[16]; double vpos, hpos, size, afterglow; DWORD hot; DWORD bg, cells, cells2; int hasCells2; char palette[16]; const char *frame; };
extern struct LcConfig cfg;
void cfg_defaults(void);
void apply_setting(const char *key, const char *val);
void clamp_settings(void);
DWORD parse_hex_bgr(const char *s);
void preset_colors(const char *name, DWORD *bg, DWORD *cells);
#endif
