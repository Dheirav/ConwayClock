// Settings window: one control per ini key. Every change is written back to
// life-clock.ini in place (comments kept), and the running wallpaper reloads
// the file within two seconds, so nothing here talks to the wallpaper
// directly. Runs as its own process: life-clock.exe --settings
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"

static char iniPath[MAX_PATH], exePath[MAX_PATH], exeDir[MAX_PATH];
static HFONT font; static int dpi = 96;
#define S(px) MulDiv(px, dpi, 96)

// ---- ini read/write (values only; comments and order preserved) ---------------
static char *ini_get(const char *key, char *buf, size_t n, const char *dflt) {
  strncpy(buf, dflt, n); buf[n - 1] = 0;
  FILE *f = fopen(iniPath, "r"); if (!f) return buf;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char *p = line; while (*p == ' ' || *p == '\t') p++; if (*p == ';' || *p == '#') continue;
    char *eq = strchr(p, '='); if (!eq) continue; *eq = 0; char *k = p, *e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    if (strcmp(k, key)) continue;
    char *v = eq + 1; while (*v == ' ' || *v == '\t') v++; e = v + strlen(v); while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    char *c = strchr(v, ';'); if (c) { *c = 0; e = v + strlen(v); while (e > v && e[-1] == ' ') *--e = 0; }
    strncpy(buf, v, n); buf[n - 1] = 0; break;
  }
  fclose(f); return buf;
}
static void ini_remove(const char *key) {
  FILE *f = fopen(iniPath, "rb"); if (!f) return;
  fseek(f, 0, SEEK_END); size_t len = ftell(f); fseek(f, 0, SEEK_SET);
  char *text = malloc(len + 1); if (fread(text, 1, len, f) != len) { fclose(f); free(text); return; } text[len] = 0; fclose(f);
  char *out = malloc(len + 1); out[0] = 0; char *p = text;
  while (*p) {
    char *nl = strchr(p, '\n'); size_t l = nl ? (size_t)(nl - p + 1) : strlen(p);
    char line[256]; size_t cl = l < 255 ? l : 255; memcpy(line, p, cl); line[cl] = 0;
    char *q = line; while (*q == ' ' || *q == '\t') q++;
    char *eq = strchr(q, '='); int match = 0;
    if (*q != ';' && *q != '#' && eq) { char k[64]; size_t kl = eq - q;
      if (kl < 63) { memcpy(k, q, kl); k[kl] = 0; char *e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0; match = !strcmp(k, key); } }
    if (!match) strncat(out, p, l);
    p += l;
  }
  f = fopen(iniPath, "wb"); if (f) { fwrite(out, 1, strlen(out), f); fclose(f); }
  free(text); free(out);
}
static void ini_set(const char *key, const char *val) {
  char *text = NULL; size_t len = 0; FILE *f = fopen(iniPath, "rb");
  if (f) { fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); text = malloc(len + 1); fread(text, 1, len, f); text[len] = 0; fclose(f); } else { text = calloc(1, 1); }
  char *out = malloc(len + 512); out[0] = 0; int done = 0;
  char *p = text;
  while (*p) {
    char *nl = strchr(p, '\n'); size_t l = nl ? (size_t)(nl - p + 1) : strlen(p); char line[256]; size_t cl = l < 255 ? l : 255; memcpy(line, p, cl); line[cl] = 0;
    char *q = line; while (*q == ' ' || *q == '\t') q++;
    char *eq = strchr(q, '='); int match = 0;
    if (!done && *q != ';' && *q != '#' && eq) { char k[64]; size_t kl = eq - q; if (kl < 63) { memcpy(k, q, kl); k[kl] = 0; char *e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0; match = !strcmp(k, key); } }
    if (match) { char rep[256]; snprintf(rep, sizeof rep, "%s = %s\r\n", key, val); strcat(out, rep); done = 1; } else strncat(out, p, l);
    p += l;
  }
  if (!done) { char rep[256]; snprintf(rep, sizeof rep, "%s%s = %s\r\n", (len && text[len - 1] != '\n') ? "\r\n" : "", key, val); strcat(out, rep); }
  f = fopen(iniPath, "wb"); if (f) { fwrite(out, 1, strlen(out), f); fclose(f); }
  free(text); free(out);
}

// ---- controls -------------------------------------------------------------------
typedef struct { const char *key, *label; int type; const char *opts; int lo, hi; int id; HWND h, hv; const char *dflt; } Ctl;
enum { T_COMBO, T_SLIDER, T_CHECK, T_COLOR };
static Ctl ctls[] = {
  { "palette",     "Palette",                 T_COMBO,  "amber|green|white|blue|red", 0, 0, 100, 0, 0, "amber" },
  { "bg",          "Background colour",       T_COLOR,  NULL, 0, 0, 101, 0, 0, "07090f" },
  { "cells",       "Cell colour",             T_COLOR,  NULL, 0, 0, 102, 0, 0, "ffe9a8" },
  { "cells2",      "Dense-area colour (or none)", T_COLOR, NULL, 0, 0, 103, 0, 0, "none" },
  { "gain",        "Brightness",              T_SLIDER, NULL, 5, 120, 104, 0, 0, "40" },
  { "fps",         "Frames per second",       T_COMBO,  "3|6|12|24", 0, 0, 105, 0, 0, "6" },
  { "battery_fps", "Frames per second on battery", T_COMBO, "1|3|6|12|24", 0, 0, 106, 0, 0, "3" },
  { "view",        "View",                    T_COMBO,  "whole|display", 0, 0, 107, 0, 0, "whole" },
  { "zoom",        "Zoom (cells per pixel)",  T_COMBO,  "auto|8|4|2|1", 0, 0, 108, 0, 0, "auto" },
  { "size",        "Size (%)",                T_SLIDER, NULL, 30, 200, 109, 0, 0, "100" },
  { "hpos",        "Horizontal position (%)", T_SLIDER, NULL, 0, 100, 110, 0, 0, "50" },
  { "vpos",        "Vertical position (%)",   T_SLIDER, NULL, 20, 90, 111, 0, 0, "50" },
  { "monitor",     "Monitor (0 = primary)",   T_COMBO,  "0", 0, 0, 127, 0, 0, "0" },
  { "pm",          "AM/PM indicator",         T_COMBO,  "dot|text|machine|hide", 0, 0, 112, 0, 0, "dot" },
  { "colon",       "Colon",                   T_COMBO,  "pulse|machine|hide", 0, 0, 113, 0, 0, "pulse" },
  { "highlight",   "Highlight changing cells", T_CHECK, NULL, 0, 0, 114, 0, 0, "0" },
  { "hot",         "Highlight colour",        T_COLOR,  NULL, 0, 0, 115, 0, 0, "c8e9ff" },
  { "afterglow",   "Afterglow (%)",           T_SLIDER, NULL, 0, 95, 116, 0, 0, "0" },
  { "tour",        "Tour (watch mode / screensaver)", T_COMBO, "auto|1|0", 0, 0, 117, 0, 0, "auto" },
  { "status",      "Show status line",        T_CHECK,  NULL, 0, 0, 118, 0, 0, "0" },
  { "theme",       "Day / night",             T_COMBO,  "off|clock|system", 0, 0, 119, 0, 0, "off" },
  { "day_palette", "Day palette",             T_COMBO,  "white|blue|green|amber|red", 0, 0, 120, 0, 0, "white" },
  { "night_palette", "Night palette",         T_COMBO,  "amber|red|green|blue|white", 0, 0, 121, 0, 0, "amber" },
  { "day_gain",    "Day brightness",          T_SLIDER, NULL, 5, 120, 122, 0, 0, "40" },
  { "night_gain",  "Night brightness",        T_SLIDER, NULL, 5, 120, 123, 0, 0, "22" },
  { "day_start",   "Day starts at (hour)",    T_SLIDER, NULL, 0, 23, 124, 0, 0, "7" },
  { "night_start", "Night starts at (hour)",  T_SLIDER, NULL, 0, 23, 125, 0, 0, "19" },
  { "fade",        "Fade between them (s)",   T_SLIDER, NULL, 0, 20, 126, 0, 0, "3" },
};
#define NCTL (int)(sizeof ctls / sizeof ctls[0])
// The monitor list depends on the machine, so the combo is filled in at run time
// rather than declared. A value in the ini naming a monitor that is not attached
// is still offered, so opening the window does not silently discard it.
static char monitorOpts[96];
static void build_monitor_options(void) {
  int n = GetSystemMetrics(SM_CMONITORS); if (n < 1) n = 1;
  char cur[16]; ini_get("monitor", cur, sizeof cur, "0");   // the file is flat key = value, not sectioned
  int want = atoi(cur); if (want + 1 > n) n = want + 1;
  if (n > 16) n = 16;
  char *p = monitorOpts; *p = 0;
  for (int i = 0; i < n; i++) p += sprintf(p, i ? "|%d" : "%d", i);
  for (int i = 0; i < NCTL; i++) if (!strcmp(ctls[i].key, "monitor")) ctls[i].opts = monitorOpts;
}
#define ID_STARTUP 200
#define ID_WATCH 201
#define ID_OPENINI 202
#define ID_DEFAULTS 203
#define ID_CLOSE 204
static HWND startupChk;


static int is_pct(const Ctl *c) { return !strcmp(c->key, "size") || !strcmp(c->key, "hpos") || !strcmp(c->key, "vpos") || !strcmp(c->key, "afterglow"); }
static DWORD hex_to_colorref(const char *s) { unsigned v = (unsigned)strtoul(s[0] == '#' ? s + 1 : s, NULL, 16); return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }
static void colorref_to_hex(COLORREF c, char *out) { sprintf(out, "%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c)); }

static void load_into_controls(void) {
  char v[64];
  for (int i = 0; i < NCTL; i++) { Ctl *c = &ctls[i]; ini_get(c->key, v, sizeof v, c->dflt);
    if (c->type == T_COMBO) { int idx = 0, k = 0; const char *o = c->opts; char tok[32]; while (*o) { const char *b = strchr(o, '|'); size_t l = b ? (size_t)(b - o) : strlen(o); memcpy(tok, o, l); tok[l] = 0; if (!strcmp(tok, v)) idx = k; k++; o += l + (b ? 1 : 0); } SendMessageA(c->h, CB_SETCURSEL, idx, 0); }
    else if (c->type == T_SLIDER) { double d = atof(v); int val = is_pct(c) ? (int)(d * 100 + 0.5) : (int)d; if (!strcmp(c->key, "size") && d <= 2.0 && strchr(v, '.')) val = (int)(d * 100 + 0.5); SendMessageA(c->h, TBM_SETPOS, TRUE, val); char t[16]; sprintf(t, "%d", val); SetWindowTextA(c->hv, t); }
    else if (c->type == T_CHECK) SendMessageA(c->h, BM_SETCHECK, (atoi(v) || !strcmp(v, "true")) ? BST_CHECKED : BST_UNCHECKED, 0);
    else if (c->type == T_COLOR) SetWindowTextA(c->h, v);
  }
  { char t[32]; ini_get("theme", t, sizeof t, "off"); int themed = strcmp(t, "off") != 0;
    for (int i = 0; i < NCTL; i++) { const char *k = ctls[i].key;
      int single = !strcmp(k, "palette") || !strcmp(k, "bg") || !strcmp(k, "cells") || !strcmp(k, "gain");
      int dn = !strncmp(k, "day_", 4) || !strncmp(k, "night_", 6) || !strcmp(k, "fade");
      if (single) EnableWindow(ctls[i].h, !themed); else if (dn) EnableWindow(ctls[i].h, themed); } }
  char base[MAX_PATH]; SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, base); strcat(base, "\\Life Clock.lnk");
  SendMessageA(startupChk, BM_SETCHECK, GetFileAttributesA(base) != INVALID_FILE_ATTRIBUTES ? BST_CHECKED : BST_UNCHECKED, 0);
}
static void save_control(Ctl *c) {
  char v[64];
  if (c->type == T_COMBO) { int idx = (int)SendMessageA(c->h, CB_GETCURSEL, 0, 0); SendMessageA(c->h, CB_GETLBTEXT, idx, (LPARAM)v); }
  else if (c->type == T_SLIDER) { int val = (int)SendMessageA(c->h, TBM_GETPOS, 0, 0); char t[16]; sprintf(t, "%d", val); SetWindowTextA(c->hv, t); if (is_pct(c)) sprintf(v, "%.2f", val / 100.0); else sprintf(v, "%d", val); }
  else if (c->type == T_CHECK) strcpy(v, SendMessageA(c->h, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
  else { GetWindowTextA(c->h, v, sizeof v); }
  ini_set(c->key, v);
  if (!strcmp(c->key, "palette") || !strcmp(c->key, "theme")) load_into_controls(); // palette presets change bg/cells in the program, not the file; nothing to do
}
static void pick_color(Ctl *c) {
  char cur[64]; GetWindowTextA(c->h, cur, sizeof cur);
  static COLORREF custom[16]; CHOOSECOLORA cc = { sizeof cc }; cc.hwndOwner = GetParent(c->h); cc.lpCustColors = custom; cc.rgbResult = strcmp(cur, "none") ? hex_to_colorref(cur) : RGB(255, 233, 168); cc.Flags = CC_FULLOPEN | CC_RGBINIT;
  if (ChooseColorA(&cc)) { char hex[16]; colorref_to_hex(cc.rgbResult, hex); SetWindowTextA(c->h, hex); ini_set(c->key, hex); if (!strcmp(c->key, "bg") || !strcmp(c->key, "cells")) ini_set("palette", "custom"); load_into_controls(); }
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_COMMAND: {
      int id = LOWORD(w), code = HIWORD(w);
      if (id == ID_CLOSE) { DestroyWindow(h); return 0; }
      if (id == ID_OPENINI) { ShellExecuteA(NULL, "open", iniPath, NULL, exeDir, SW_SHOWNORMAL); return 0; }
      if (id == ID_WATCH) { ShellExecuteA(NULL, "open", exePath, "--fullscreen 1", exeDir, SW_SHOWNORMAL); return 0; }
      if (id == ID_DEFAULTS) {
        // bg and cells are absent from the shipped file, so writing their
        // defaults appends them *after* palette; load_ini applies in file order
        // and last wins, which would leave the palette drop-down doing nothing.
        // Their default is "whatever the palette says", which is to have no line.
        for (int i = 0; i < NCTL; i++) {
          if (!strcmp(ctls[i].key, "bg") || !strcmp(ctls[i].key, "cells")) ini_remove(ctls[i].key);
          else ini_set(ctls[i].key, ctls[i].dflt); }
        load_into_controls(); return 0; }
      if (id == ID_STARTUP) { ShellExecuteA(NULL, "open", exePath, SendMessageA(startupChk, BM_GETCHECK, 0, 0) == BST_CHECKED ? "--install-startup" : "--uninstall-startup", exeDir, SW_HIDE); return 0; }
      for (int i = 0; i < NCTL; i++) if (ctls[i].id == id) {
        if (ctls[i].type == T_COMBO && code == CBN_SELCHANGE) save_control(&ctls[i]);
        else if (ctls[i].type == T_CHECK && code == BN_CLICKED) save_control(&ctls[i]);
        else if (ctls[i].type == T_COLOR && code == BN_CLICKED) { if (!strcmp(ctls[i].key, "cells2") && (GetKeyState(VK_SHIFT) & 0x8000)) { SetWindowTextA(ctls[i].h, "none"); ini_set("cells2", "none"); } else pick_color(&ctls[i]); }
      }
      return 0; }
    case WM_HSCROLL: for (int i = 0; i < NCTL; i++) if (ctls[i].type == T_SLIDER && ctls[i].h == (HWND)l) { char t[16]; sprintf(t, "%d", (int)SendMessageA(ctls[i].h, TBM_GETPOS, 0, 0)); SetWindowTextA(ctls[i].hv, t); if (LOWORD(w) == TB_ENDTRACK || LOWORD(w) == TB_THUMBPOSITION || LOWORD(w) == TB_LINEUP || LOWORD(w) == TB_LINEDOWN || LOWORD(w) == TB_PAGEUP || LOWORD(w) == TB_PAGEDOWN) save_control(&ctls[i]); } return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcA(h, m, w, l);
}
static HWND mk(HWND parent, const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id) {
  HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL); SendMessageA(c, WM_SETFONT, (WPARAM)font, TRUE); return c;
}
int settings_main(const char *ini, const char *exe, const char *dir) {
  strcpy(iniPath, ini); strcpy(exePath, exe); strcpy(exeDir, dir);
  HANDLE mutex = CreateMutexA(NULL, TRUE, "LifeClockSettings"); if (GetLastError() == ERROR_ALREADY_EXISTS) { HWND e = FindWindowA("LifeClockSettings", NULL); if (e) SetForegroundWindow(e); return 0; }
  INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);
  build_monitor_options();
  typedef UINT (WINAPI *GDFS)(void); GDFS gdfs = (GDFS)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem"); if (gdfs) dpi = gdfs();
  font = CreateFontA(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
  WNDCLASSA wc = { 0 }; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "LifeClockSettings"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1)); RegisterClassA(&wc);
  const int NCOL = 3, LW = S(178), CW = S(186), ROW = S(30), PAD = S(14), COLW = LW + CW + S(22);
  int rows = (NCTL + NCOL - 1) / NCOL, W = NCOL * COLW + PAD, H = PAD + rows * ROW + S(78);
  RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
  HWND h = CreateWindowExA(0, wc.lpszClassName, "Life Clock settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
  for (int i = 0; i < NCTL; i++) { Ctl *c = &ctls[i]; int col = i / rows, row = i % rows; int x = PAD + col * COLW, y = PAD + row * ROW;
    mk(h, "STATIC", c->label, SS_LEFT, x, y + S(6), LW, S(20), 0);
    if (c->type == T_COMBO) { c->h = mk(h, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + LW, y + S(2), CW, S(200), c->id); const char *o = c->opts; char tok[32]; while (*o) { const char *b = strchr(o, '|'); size_t l = b ? (size_t)(b - o) : strlen(o); memcpy(tok, o, l); tok[l] = 0; SendMessageA(c->h, CB_ADDSTRING, 0, (LPARAM)tok); o += l + (b ? 1 : 0); } }
    else if (c->type == T_SLIDER) { c->h = mk(h, TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS, x + LW, y, CW - S(36), S(26), c->id); SendMessageA(c->h, TBM_SETRANGE, TRUE, MAKELONG(c->lo, c->hi)); c->hv = mk(h, "STATIC", "", SS_RIGHT, x + LW + CW - S(34), y + S(6), S(32), S(20), 0); }
    else if (c->type == T_CHECK) c->h = mk(h, "BUTTON", "", BS_AUTOCHECKBOX, x + LW, y + S(4), S(20), S(20), c->id);
    else c->h = mk(h, "BUTTON", "", BS_PUSHBUTTON, x + LW, y + S(1), S(86), S(24), c->id);
  }
  int by = PAD + rows * ROW + S(14);
  startupChk = mk(h, "BUTTON", "Start with Windows", BS_AUTOCHECKBOX, PAD, by + S(4), S(170), S(22), ID_STARTUP);
  mk(h, "BUTTON", "Watch full screen", BS_PUSHBUTTON, PAD + S(190), by, S(130), S(26), ID_WATCH);
  mk(h, "BUTTON", "Open ini file", BS_PUSHBUTTON, PAD + S(330), by, S(110), S(26), ID_OPENINI);
  mk(h, "BUTTON", "Reset to defaults", BS_PUSHBUTTON, W - PAD - S(230), by, S(120), S(26), ID_DEFAULTS);
  mk(h, "BUTTON", "Close", BS_DEFPUSHBUTTON, W - PAD - S(100), by, S(100), S(26), ID_CLOSE);
  mk(h, "STATIC", "Changes apply to the running wallpaper within two seconds. Shift-click a colour button for 'none'.", SS_LEFT, PAD, by + S(34), W - 2 * PAD, S(20), 0);
  load_into_controls();
  MSG msg; while (GetMessageA(&msg, NULL, 0, 0)) { if (!IsDialogMessageA(h, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); } }
  CloseHandle(mutex); return 0;
}
