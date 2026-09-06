// The settings layer: every ini key parsed, and the clamping that keeps a
// hand-edited file from producing a broken view. main.c cannot be built without
// windows.h, which is why this lives in config.c and is tested here.
#include "config.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void check(const char *what, long got, long want) {
  if (got != want) { printf("  FAIL %-34s got %ld, want %ld\n", what, got, want); fails++; }
}
static void checkd(const char *what, double got, double want) {
  if (got < want - 1e-9 || got > want + 1e-9) { printf("  FAIL %-34s got %g, want %g\n", what, got, want); fails++; }
}
static void set(const char *k, const char *v) { apply_setting(k, v); }

int main(void) {
  // defaults
  cfg_defaults();
  check("default fps", cfg.fps, 6);
  check("default battery_fps", cfg.batteryFps, 3);
  check("default gain", cfg.gain, 40);
  check("default attach", cfg.attach, 7);
  check("default pm = dot", cfg.pmMode, 3);
  check("default colon = pulse", cfg.colonMode, 1);
  check("default tour = auto", cfg.tour, -1);
  checkd("default size", cfg.size, 1.0);
  checkd("default hpos", cfg.hpos, 0.5);

  // the enumerated keys
  set("view", "display");     check("view = display", cfg.view, 1);
  set("view", "whole");       check("view = whole", cfg.view, 0);
  set("pm", "text");          check("pm = text", cfg.pmMode, 2);
  set("pm", "machine");       check("pm = machine", cfg.pmMode, 0);
  set("pm", "hide");          check("pm = hide", cfg.pmMode, 1);
  set("pm", "dot");           check("pm = dot", cfg.pmMode, 3);
  set("colon", "machine");    check("colon = machine", cfg.colonMode, 0);
  set("colon", "hide");       check("colon = hide", cfg.colonMode, 2);
  set("theme", "clock");      check("theme = clock", cfg.theme, 1);
  set("theme", "system");     check("theme = system", cfg.theme, 2);
  set("theme", "off");        check("theme = off", cfg.theme, 0);
  set("zoom", "auto");        check("zoom = auto", cfg.zoom, 0);
  set("zoom", "4");           check("zoom = 4", cfg.zoom, 4);
  set("tour", "auto");        check("tour = auto", cfg.tour, -1);
  set("highlight", "true");   check("highlight = true", cfg.highlight, 1);
  set("status", "on");        check("status = on", cfg.status, 1);

  // colours are stored BGR, so RRGGBB is byte-reversed
  check("parse_hex_bgr(ff0000)", (long)parse_hex_bgr("ff0000"), 0x0000ff);
  check("parse_hex_bgr(#00ff00)", (long)parse_hex_bgr("#00ff00"), 0x00ff00);
  set("palette", "green");
  check("palette green sets bg", (long)cfg.bg, (long)parse_hex_bgr("040a06"));
  check("palette green sets cells", (long)cfg.cells, (long)parse_hex_bgr("5cff7a"));
  set("cells2", "none");      check("cells2 = none clears the flag", cfg.hasCells2, 0);
  set("cells2", "112233");    check("cells2 set raises the flag", cfg.hasCells2, 1);
  { DWORD bg = 0, cells = 0; preset_colors("amber", &bg, &cells);
    check("preset_colors(amber)", (long)cells, (long)parse_hex_bgr("ffe9a8")); }

  // clamping: a hand-edited file must not be able to break the view
  cfg_defaults();
  set("fps", "7"); set("battery_fps", "99"); set("gain", "9999");
  set("size", "50"); set("hpos", "-3"); set("vpos", "17");
  set("zoom", "5"); set("afterglow", "4"); set("fade", "999");
  set("day_start", "40"); set("night_start", "-1");
  clamp_settings();
  check("fps snapped to a legal value", cfg.fps, 6);
  check("battery_fps snapped", cfg.batteryFps, 3);
  check("gain capped", cfg.gain, 255);
  checkd("size capped", cfg.size, 2.0);
  checkd("hpos floored", cfg.hpos, 0.0);
  checkd("vpos capped", cfg.vpos, 0.9);
  check("zoom rejected", cfg.zoom, 0);
  checkd("afterglow capped", cfg.afterglow, 0.95);
  checkd("fade capped", cfg.fadeSec, 30);
  check("day_start rejected", cfg.dayStart, 7);
  check("night_start rejected", cfg.nightStart, 19);

  // clamping must leave legal values alone
  cfg_defaults();
  set("fps", "24"); set("gain", "60"); set("size", "0.5"); set("vpos", "0.78"); set("zoom", "8");
  clamp_settings();
  check("fps 24 kept", cfg.fps, 24);
  check("gain 60 kept", cfg.gain, 60);
  checkd("size 0.5 kept", cfg.size, 0.5);
  checkd("vpos 0.78 kept", cfg.vpos, 0.78);
  check("zoom 8 kept", cfg.zoom, 8);

  // an unknown key must be ignored, not crash or corrupt anything
  cfg_defaults(); set("nonsense", "42"); set("fps", "12"); clamp_settings();
  check("unknown key ignored", cfg.fps, 12);

  printf("settings: %s\n", fails ? "FAILURES ABOVE" : "all checks passed");
  return fails ? 1 : 0;
}
