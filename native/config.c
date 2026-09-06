#include "config.h"
#include <stdlib.h>
#include <string.h>

struct LcConfig cfg;
void cfg_defaults(void) { memset(&cfg, 0, sizeof cfg); cfg.pmMode = 3; cfg.colonMode = 1; cfg.tour = -1; cfg.hot = 0xffe9c8; /* BGR: light cyan-white */
  cfg.dayStart = 7; cfg.nightStart = 19; cfg.dayGain = 40; cfg.nightGain = 22; cfg.fadeSec = 3; strcpy(cfg.dayPalette, "white"); strcpy(cfg.nightPalette, "amber"); cfg.fps = 6; cfg.batteryFps = 3; cfg.gain = 40; cfg.attach = 7; cfg.vpos = 0.5; cfg.hpos = 0.5; cfg.size = 1.0; cfg.bg = 0x0f0907; cfg.cells = 0xa8e9ff; strcpy(cfg.palette, "amber"); }
static const char *PRESETS[][3] = { { "amber", "07090f", "ffe9a8" }, { "green", "040a06", "5cff7a" }, { "white", "0a0a0a", "f2f2f2" }, { "blue", "070a14", "8cd2ff" }, { "red", "0c0605", "ff6b57" } };
DWORD parse_hex_bgr(const char *s) { unsigned v = (unsigned)strtoul(s[0] == '#' ? s + 1 : s, NULL, 16); return ((v & 0xff) << 16) | (v & 0xff00) | ((v >> 16) & 0xff); }
// A palette name or an RRGGBB colour.
void preset_colors(const char *name, DWORD *bg, DWORD *cells) {
  for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) if (!strcmp(name, PRESETS[i][0])) { *bg = parse_hex_bgr(PRESETS[i][1]); *cells = parse_hex_bgr(PRESETS[i][2]); return; }
  *cells = parse_hex_bgr(name);
}
int apply_setting(const char *key, const char *val) {
  if (!strcmp(key, "fps")) cfg.fps = atoi(val);
  else if (!strcmp(key, "battery_fps")) cfg.batteryFps = atoi(val);
  else if (!strcmp(key, "view")) cfg.view = !strcmp(val, "display");
  else if (!strcmp(key, "gain")) cfg.gain = atoi(val);
  else if (!strcmp(key, "size")) cfg.size = atof(val);
  else if (!strcmp(key, "hpos")) cfg.hpos = atof(val);
  else if (!strcmp(key, "vpos")) cfg.vpos = atof(val);
  else if (!strcmp(key, "monitor")) cfg.monitor = atoi(val);
  else if (!strcmp(key, "status")) cfg.status = atoi(val) || !strcmp(val, "on") || !strcmp(val, "true");
  else if (!strcmp(key, "attach")) cfg.attach = atoi(val);
  else if (!strcmp(key, "pm")) cfg.pmMode = !strcmp(val, "hide") ? 1 : !strcmp(val, "machine") ? 0 : !strcmp(val, "text") ? 2 : 3; // 3 = dot (default)
  else if (!strcmp(key, "afterglow")) cfg.afterglow = atof(val);
  else if (!strcmp(key, "tour")) cfg.tour = !strcmp(val, "auto") ? -1 : (atoi(val) || !strcmp(val, "true"));
  else if (!strcmp(key, "highlight")) cfg.highlight = atoi(val) || !strcmp(val, "true");
  else if (!strcmp(key, "hot")) cfg.hot = parse_hex_bgr(val);
  else if (!strcmp(key, "frames")) cfg.frames = atoi(val);
  else if (!strcmp(key, "frame_step")) cfg.frameStep = atoi(val);
  else if (!strcmp(key, "theme")) cfg.theme = !strcmp(val, "clock") ? 1 : !strcmp(val, "system") ? 2 : 0;
  else if (!strcmp(key, "day_start")) cfg.dayStart = atoi(val);
  else if (!strcmp(key, "night_start")) cfg.nightStart = atoi(val);
  else if (!strcmp(key, "day_palette")) { strncpy(cfg.dayPalette, val, 15); cfg.dayPalette[15] = 0; }
  else if (!strcmp(key, "night_palette")) { strncpy(cfg.nightPalette, val, 15); cfg.nightPalette[15] = 0; }
  else if (!strcmp(key, "day_gain")) cfg.dayGain = atoi(val);
  else if (!strcmp(key, "night_gain")) cfg.nightGain = atoi(val);
  else if (!strcmp(key, "fade")) cfg.fadeSec = atof(val);
  else if (!strcmp(key, "zoom")) cfg.zoom = !strcmp(val, "auto") ? 0 : atoi(val);
  else if (!strcmp(key, "fullscreen")) cfg.fullscreen = atoi(val) || !strcmp(val, "true");
  else if (!strcmp(key, "colon")) cfg.colonMode = !strcmp(val, "machine") ? 0 : !strcmp(val, "hide") ? 2 : 1; // 1 = pulse (default)
  else if (!strcmp(key, "palette")) { strncpy(cfg.palette, val, 15); for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) if (!strcmp(val, PRESETS[i][0])) { cfg.bg = parse_hex_bgr(PRESETS[i][1]); cfg.cells = parse_hex_bgr(PRESETS[i][2]); cfg.hasCells2 = 0; } }
  else if (!strcmp(key, "bg")) cfg.bg = parse_hex_bgr(val);
  else if (!strcmp(key, "cells")) cfg.cells = parse_hex_bgr(val);
  else if (!strcmp(key, "cells2")) { if (val[0] && strcmp(val, "none")) { cfg.cells2 = parse_hex_bgr(val); cfg.hasCells2 = 1; } else cfg.hasCells2 = 0; }
  else return 0;   // not a key we know
  return 1;
}
int clamp_settings(void) {
  int n = 0;
  #define CLAMP(cond, fix) do { if (cond) { fix; n++; } } while (0)
  CLAMP(cfg.fps != 3 && cfg.fps != 6 && cfg.fps != 12 && cfg.fps != 24, cfg.fps = 6);
  CLAMP(cfg.batteryFps != 1 && cfg.batteryFps != 3 && cfg.batteryFps != 6 && cfg.batteryFps != 12 && cfg.batteryFps != 24, cfg.batteryFps = 3);
  CLAMP(cfg.gain < 1, cfg.gain = 1); CLAMP(cfg.gain > 255, cfg.gain = 255);
  CLAMP(cfg.dayStart < 0 || cfg.dayStart > 23, cfg.dayStart = 7);
  CLAMP(cfg.nightStart < 0 || cfg.nightStart > 23, cfg.nightStart = 19);
  CLAMP(cfg.dayGain < 1, cfg.dayGain = 1); CLAMP(cfg.dayGain > 255, cfg.dayGain = 255);
  CLAMP(cfg.nightGain < 1, cfg.nightGain = 1); CLAMP(cfg.nightGain > 255, cfg.nightGain = 255);
  CLAMP(cfg.fadeSec < 0, cfg.fadeSec = 0); CLAMP(cfg.fadeSec > 30, cfg.fadeSec = 30);
  CLAMP(cfg.afterglow < 0, cfg.afterglow = 0); CLAMP(cfg.afterglow > 0.95, cfg.afterglow = 0.95);
  CLAMP(cfg.zoom != 0 && cfg.zoom != 1 && cfg.zoom != 2 && cfg.zoom != 4 && cfg.zoom != 8 && cfg.zoom != 16, cfg.zoom = 0);
  CLAMP(cfg.size < 0.3, cfg.size = 0.3); CLAMP(cfg.size > 2.0, cfg.size = 2.0);
  CLAMP(cfg.hpos < 0.0, cfg.hpos = 0.0); CLAMP(cfg.hpos > 1.0, cfg.hpos = 1.0);
  CLAMP(cfg.vpos < 0.2, cfg.vpos = 0.2); CLAMP(cfg.vpos > 0.9, cfg.vpos = 0.9);
  #undef CLAMP
  return n;
}
