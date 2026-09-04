// Life Clock, native Windows wallpaper.
//
// Runs dim's Game-of-Life digital clock (clock.rle) in real time with the
// Hashlife engine in hashlife.c, and draws it in a window parented behind
// the desktop icons, presenting a frame only when a new generation lands.
// Steps stop whenever the desktop is covered, the session is locked or the
// display is off; on return the machine catches up in one jump.
//
// Settings live in life-clock.ini next to the executable (created with
// defaults and comments on first run) and are reloaded live when the file
// changes. Command-line flags with the same names override the file:
//   life-clock.exe --fullscreen 1        watch mode: a normal window at 1/4 zoom, Esc closes
//   life-clock.exe --install-startup | --uninstall-startup   add/remove the Startup-folder shortcut
//   life-clock.exe --settings            the settings window (also from the tray menu)
//   life-clock.exe [--fps N] [--battery_fps N] [--view whole|display] [--palette NAME]
//                  [--bg RRGGBB] [--cells RRGGBB] [--cells2 RRGGBB] [--gain N]
//                  [--size F] [--hpos F] [--vpos F] [--monitor N] [--status 0|1]
//                  [--quit] [--frame out.bmp]   (headless: sync, render once, exit)
// Log: life-clock.log next to the executable.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <shlobj.h>
#include <objbase.h>
#include "hashlife.h"
#include "inflate.h"
#include "colon.h"
#include "settings.h"

extern const int SNAP_COUNT; extern const int SNAP_MINUTE[]; extern const size_t SNAP_OFF[]; extern const unsigned char SNAP_DATA[];

#define PERIOD 11520
#define GPS 192
#define DISPLAY_LAG 11450   // see index.html: never-early display
#define PAT_W 10016
#define PAT_H 6796
static const struct { int x, y, w, h; } DISPLAY = { 1900, 3950, 8000, 2850 };

static struct { int fps, batteryFps, view, gain, status, attach, monitor, pmMode, colonMode, zoom, fullscreen, screensaver, tour, highlight, frames, frameStep; double vpos, hpos, size, afterglow; DWORD hot; DWORD bg, cells, cells2; int hasCells2; char palette[16]; const char *frame; } cfg;
static void cfg_defaults(void) { memset(&cfg, 0, sizeof cfg); cfg.pmMode = 3; cfg.colonMode = 1; cfg.tour = -1; cfg.hot = 0xffe9c8; /* BGR: light cyan-white */ cfg.fps = 6; cfg.batteryFps = 3; cfg.gain = 40; cfg.attach = 7; cfg.vpos = 0.5; cfg.hpos = 0.5; cfg.size = 1.0; cfg.bg = 0x0f0907; cfg.cells = 0xa8e9ff; strcpy(cfg.palette, "amber"); }
static int g_argc; static char **g_argv; static char iniPath[MAX_PATH]; static FILETIME iniTime;
static FILE *logfile; static char logPath[MAX_PATH]; static int logDay = -1;
// One log per day: at the first message of a new day the current log becomes
// life-clock.prev.log (replacing the old one), so at most two days are kept.
static void log_rotate_if_needed(const SYSTEMTIME *t) {
  if (logDay == t->wDay) return;
  if (logDay != -1 && logfile) { fclose(logfile); char prev[MAX_PATH]; snprintf(prev, sizeof prev, "%.*sprev.log", (int)(strlen(logPath) - 3), logPath); MoveFileExA(logPath, prev, MOVEFILE_REPLACE_EXISTING); logfile = fopen(logPath, "a"); }
  logDay = t->wDay;
}
static void logmsg(const char *fmt, ...) { if (!logfile) return; SYSTEMTIME t; GetLocalTime(&t); log_rotate_if_needed(&t); fprintf(logfile, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond); va_list a; va_start(a, fmt); vfprintf(logfile, fmt, a); va_end(a); fputc('\n', logfile); fflush(logfile); }

// ---- time model -------------------------------------------------------------
// The machine's cycle is 24 hours: generation 0 shows 12:00 with the PM
// indicator lit, so minute M of the cycle is M minutes after noon.
static int cycle_minute(const SYSTEMTIME *t) { return ((t->wHour + 12) % 24) * 60 + t->wMinute; }
static int64_t target_generation(void) {
  SYSTEMTIME t; GetLocalTime(&t);
  double s = t.wSecond + t.wMilliseconds / 1000.0;
  return (int64_t)cycle_minute(&t) * PERIOD + DISPLAY_LAG + (int64_t)(s * GPS);
}
static int current_minute(void) { SYSTEMTIME t; GetLocalTime(&t); return cycle_minute(&t); }

// ---- universe ----------------------------------------------------------------
static Universe uni;
static int load_snapshot(int minute) {
  int best = 0; for (int i = 0; i < SNAP_COUNT; i++) if (SNAP_MINUTE[i] <= minute) best = i;
  size_t len; uint8_t *text = gunzip(SNAP_DATA + SNAP_OFF[best], SNAP_OFF[best + 1] - SNAP_OFF[best], &len);
  if (!text) { logmsg("snapshot %d: gunzip failed", best); return 0; }
  int32_t n; Cell *cells = hl_parse_rle((const char *)text, 1, &n); free(text);
  cells = colon_apply(cells, &n, cfg.colonMode);
  hl_from_cells(&uni, cells, n); free(cells);
  uni.generation = (int64_t)SNAP_MINUTE[best] * PERIOD;
  logmsg("loaded snapshot minute %d (%d cells) for minute %d", SNAP_MINUTE[best], n, minute);
  return 1;
}

// ---- view & rendering ------------------------------------------------------------
static struct { int x, y, w, h, z, pw, ph; } view;
static void *dibBits; static int dibPainted; static HDC memdc;   // the screen DIB, its pixels (`pixels` aliases them once created) and its DC
static int scrW, scrH;               // output size
static uint8_t *dens;                // pw*ph density map
static uint32_t *pixels;             // scrW*scrH BGRA
static uint32_t lut[256], lutHot[256];
static int dstX, dstY, dstW, dstH;   // where the view lands on screen (letterboxed)
static int *mapX, *mapY, *fracX, *fracY; static uint8_t *hrows; // per source row, horizontally resampled to dstW

static uint32_t mix(DWORD a, DWORD b, double t) { // both BGR-packed; result BGRA opaque
  int r = (int)((a & 255) + ((int)(b & 255) - (int)(a & 255)) * t), g = (int)(((a >> 8) & 255) + ((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t), bl = (int)(((a >> 16) & 255) + ((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  return 0xff000000u | (uint32_t)(bl | (g << 8) | (r << 16));
}
// Density -> colour. Presets set bg/cells unless overridden; cells2, if set,
// makes a two-tone ramp (bg -> cells -> cells2 as density rises).
static void build_palette(void) {
  for (int i = 0; i < 256; i++) { double t = i * cfg.gain / 255.0; if (t > 1) t = 1;
    lut[i] = cfg.hasCells2 ? (t < 0.5 ? mix(cfg.bg, cfg.cells, t * 2) : mix(cfg.cells, cfg.cells2, t * 2 - 1)) : mix(cfg.bg, cfg.cells, t);
    lutHot[i] = mix(cfg.bg, cfg.hot, t); }
}
static int unitScale; // 1 when one map pixel is one screen pixel (no resample needed)
static uint8_t *glow;  // afterglow buffer, same size as the density map
static uint8_t *prevDens, *chg; // last frame's density and a decaying change map, for highlight
static void setup_view(int W, int H) {
  scrW = W; scrH = H;
  int rx, ry, rw, rh;
  if (cfg.zoom) { // fixed cells-per-pixel, view = the screen, digits' centre at (hpos, vpos)
    int z = 0; while ((1 << z) < cfg.zoom) z++;
    double dcx = DISPLAY.x + DISPLAY.w / 2.0, dcy = DISPLAY.y + DISPLAY.h / 2.0;
    view.z = z; view.pw = (int)ceil(W / cfg.size); view.ph = (int)ceil(H / cfg.size);
    view.w = view.pw << z; view.h = view.ph << z; view.x = (int)(dcx - cfg.hpos * view.w); view.y = (int)(dcy - cfg.vpos * view.h);
    free(dens); dens = calloc((size_t)view.pw * view.ph, 1); free(glow); glow = calloc((size_t)view.pw * view.ph, 1); free(prevDens); prevDens = calloc((size_t)view.pw * view.ph, 1); free(chg); chg = calloc((size_t)view.pw * view.ph, 1);
    if (!dibBits || pixels != (uint32_t *)dibBits) { free(pixels); pixels = calloc((size_t)W * H, 4); } else { uint32_t bgpx = lut[0]; for (size_t i = 0; i < (size_t)W * H; i++) pixels[i] = bgpx; } dibPainted = 0;
    dstW = (int)(view.pw * cfg.size); dstH = (int)(view.ph * cfg.size); dstX = 0; dstY = 0; unitScale = (cfg.size == 1.0);
    free(mapX); free(mapY); free(fracX); free(fracY); free(hrows);
    mapX = malloc(sizeof(int) * dstW); fracX = malloc(sizeof(int) * dstW); mapY = malloc(sizeof(int) * dstH); fracY = malloc(sizeof(int) * dstH); hrows = malloc((size_t)view.ph * dstW);
    for (int i = 0; i < dstW; i++) { double sv = (i + 0.5) / cfg.size - 0.5; if (sv < 0) sv = 0; int k = (int)sv; if (k > view.pw - 2) k = view.pw - 2; mapX[i] = k; fracX[i] = (int)((sv - k) * 256); }
    for (int i = 0; i < dstH; i++) { double sv = (i + 0.5) / cfg.size - 0.5; if (sv < 0) sv = 0; int k = (int)sv; if (k > view.ph - 2) k = view.ph - 2; mapY[i] = k; fracY[i] = (int)((sv - k) * 256); }
    logmsg("view: zoom 1/%d, %dx%d cells from (%d,%d) -> %dx%d map -> %dx%d on %dx%d", 1 << z, view.w, view.h, view.x, view.y, view.pw, view.ph, dstW, dstH, W, H);
    return;
  }
  unitScale = 0;
  if (cfg.view == 1) { rx = DISPLAY.x; ry = DISPLAY.y; rw = DISPLAY.w; rh = DISPLAY.h; } else { rx = 0; ry = 0; rw = PAT_W; rh = PAT_H; }
  int pad = (int)(rw * 0.03); rx -= pad; ry -= pad; rw += 2 * pad; rh += 2 * pad;
  int z = 0; while ((rw >> z) > W * 1.6 || (rh >> z) > H * 1.6) z++;
  // Centre horizontally on the display digits (they sit low and right within
  // the machine). Vertically, put the digits' centre at fraction vpos of the
  // view (0.5 = screen centre); the view keeps its height, so the top of the
  // machine may be cut off and the bottom may show empty space.
  double dcx = DISPLAY.x + DISPLAY.w / 2.0; double halfW = fmax(dcx - rx, rx + rw - dcx); rx = (int)(dcx - halfW); rw = (int)(2 * halfW);
  if (cfg.view == 0) { double dcy = DISPLAY.y + DISPLAY.h / 2.0; int top = (int)(dcy - cfg.vpos * rh); if (top < ry) top = ry; ry = top; }
  view.x = rx; view.y = ry; view.w = rw; view.h = rh; view.z = z; view.pw = (rw + (1 << z) - 1) >> z; view.ph = (rh + (1 << z) - 1) >> z;
  free(dens); dens = calloc((size_t)view.pw * view.ph, 1); free(glow); glow = calloc((size_t)view.pw * view.ph, 1); free(prevDens); prevDens = calloc((size_t)view.pw * view.ph, 1); free(chg); chg = calloc((size_t)view.pw * view.ph, 1);
  if (!dibBits || pixels != (uint32_t *)dibBits) { free(pixels); pixels = calloc((size_t)W * H, 4); } else { uint32_t bgpx = lut[0]; for (size_t i = 0; i < (size_t)W * H; i++) pixels[i] = bgpx; } dibPainted = 0;
  double scale = fmin((double)W / view.pw, (double)H / view.ph) * cfg.size;
  dstW = (int)(view.pw * scale); dstH = (int)(view.ph * scale);
  // The display's centre is at the middle of the view horizontally and at vpos of it vertically.
  dstX = (int)(cfg.hpos * W - dstW / 2.0); dstY = (int)(cfg.vpos * H - cfg.vpos * dstH);
  free(mapX); free(mapY); free(fracX); free(fracY); free(hrows); hrows = malloc((size_t)view.ph * dstW);
  mapX = malloc(sizeof(int) * dstW); fracX = malloc(sizeof(int) * dstW); mapY = malloc(sizeof(int) * dstH); fracY = malloc(sizeof(int) * dstH);
  for (int i = 0; i < dstW; i++) { double s = (i + 0.5) / scale - 0.5; if (s < 0) s = 0; int k = (int)s; if (k > view.pw - 2) k = view.pw - 2; mapX[i] = k; fracX[i] = (int)((s - k) * 256); }
  for (int i = 0; i < dstH; i++) { double s = (i + 0.5) / scale - 0.5; if (s < 0) s = 0; int k = (int)s; if (k > view.ph - 2) k = view.ph - 2; mapY[i] = k; fracY[i] = (int)((s - k) * 256); }
  logmsg("view: %dx%d cells at 1/%d -> %dx%d map -> %dx%d at (%d,%d) on %dx%d", rw, rh, 1 << z, view.pw, view.ph, dstW, dstH, dstX, dstY, W, H);
}
// Density map -> bilinear resample -> palette -> pixels.
// Tour waypoints (pattern coordinates): where the machine's parts are.
static const struct { int x, y; const char *what; } TOUR[] = {
  { 5900, 5375, "display" }, { 8960, 5375, "minute digits" }, { 5250, 5440, "colon" }, { 2600, 5375, "hour digits and AM/PM" },
  { 8900, 3050, "lookup table 3" }, { 6600, 3050, "lookup table 2" }, { 3700, 3050, "lookup table 1" },
  { 5300, 1700, "clock distribution" }, { 5500, 450, "timebase" }, { 5900, 3300, "counters" } };
#define TOUR_N (int)(sizeof TOUR / sizeof TOUR[0])
static const double TOUR_DWELL = 7.0, TOUR_SPEED = 220.0; // seconds at each stop; cells per second while moving
static double tourStart;
// Centre of the view at tour time t (seconds), eased between stops.
static void tour_center(double t, double *cx, double *cy) {
  double total = 0, segs[TOUR_N]; for (int i = 0; i < TOUR_N; i++) { int j = (i + 1) % TOUR_N; segs[i] = hypot(TOUR[j].x - TOUR[i].x, TOUR[j].y - TOUR[i].y) / TOUR_SPEED; total += TOUR_DWELL + segs[i]; }
  t = fmod(t, total);
  for (int i = 0; i < TOUR_N; i++) { int j = (i + 1) % TOUR_N;
    if (t < TOUR_DWELL) { *cx = TOUR[i].x; *cy = TOUR[i].y; return; } t -= TOUR_DWELL;
    if (t < segs[i]) { double u = t / segs[i]; u = u * u * (3 - 2 * u); *cx = TOUR[i].x + (TOUR[j].x - TOUR[i].x) * u; *cy = TOUR[i].y + (TOUR[j].y - TOUR[i].y) * u; return; } t -= segs[i]; }
  *cx = TOUR[0].x; *cy = TOUR[0].y;
}
static void set_center(double cx, double cy) { view.x = (int)(cx - view.w / 2.0); view.y = (int)(cy - view.h / 2.0); }
static double tRender, tResample, tPresent; static LARGE_INTEGER qpf; static int pmLit; static HFONT pmFont; static int pmFontH;
static double qnow(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart * 1000.0 / qpf.QuadPart; }
static void render(void) {
  double a = qnow();
  hl_density_cached(&uni, view.x, view.y, view.pw, view.ph, view.z, dens);
  // The machine's AM/PM indicator: a box (pattern x 100..680, y 4150..4750) that is an
  // outline in the morning and filled in the afternoon, next to a static "PM" label
  // (x 700..1450). pm=text keeps the box, blanks the label, and writes AM or PM from
  // the box's own state; pm=hide blanks both.
  if (cfg.pmMode) {
    int lx0 = ((cfg.pmMode == 2 ? 700 : 0) - view.x) >> view.z, lx1 = (1450 - view.x) >> view.z, ly0 = (4000 - view.y) >> view.z, ly1 = (4850 - view.y) >> view.z;
    if (cfg.pmMode >= 2) { long sum = 0; int bx0 = (100 - view.x) >> view.z, bx1 = (680 - view.x) >> view.z, by0 = (4150 - view.y) >> view.z, by1 = (4750 - view.y) >> view.z;
      for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) if (y >= 0 && y < view.ph && x >= 0 && x < view.pw) sum += dens[(size_t)y * view.pw + x];
      pmLit = sum > 7400; }   // measured in this region: outline 4,890 cells, filled 9,956
    if (lx0 < 0) lx0 = 0; if (ly0 < 0) ly0 = 0; if (lx1 > view.pw) lx1 = view.pw; if (ly1 > view.ph) ly1 = view.ph;
    for (int y = ly0; y < ly1; y++) memset(dens + (size_t)y * view.pw + lx0, 0, lx1 > lx0 ? lx1 - lx0 : 0);
  }
  if (cfg.highlight) { // change map: how much each map pixel differed from the last frame, decaying
    size_t n = (size_t)view.pw * view.ph;
    for (size_t i = 0; i < n; i++) { int d = dens[i] - prevDens[i]; if (d < 0) d = -d; d *= 24; if (d > 255) d = 255; int c = (chg[i] * 200) >> 8; chg[i] = (uint8_t)(d > c ? d : c); }
    memcpy(prevDens, dens, n); }
  if (cfg.afterglow > 0) { // trails: each map pixel keeps a decaying maximum of what has been there
    int k = (int)(cfg.afterglow * 256); size_t n = (size_t)view.pw * view.ph;
    for (size_t i = 0; i < n; i++) { int v = dens[i], g = glow[i]; if (v > g) g = v; dens[i] = (uint8_t)g; glow[i] = (uint8_t)((g * k) >> 8); } }
  double b = qnow(); tRender += b - a;
  const int pw = view.pw, ph = view.ph, dw = dstW;
  if (unitScale) { // one map pixel per screen pixel: palette lookup only
    int cw = pw < scrW ? pw : scrW, chh = ph < scrH ? ph : scrH;
    for (int y = 0; y < chh; y++) { const uint8_t *r = dens + (size_t)y * pw, *c = chg + (size_t)y * pw; uint32_t *out = pixels + (size_t)y * scrW;
      if (cfg.highlight) for (int x = 0; x < cw; x++) out[x] = (c[x] > 30 ? lutHot : lut)[r[x]]; else for (int x = 0; x < cw; x++) out[x] = lut[r[x]]; }
    tResample += qnow() - b; goto overlays;
  }
  // Pass 1: each source row resampled horizontally once (gather).
  for (int y = 0; y < ph; y++) { const uint8_t *r = dens + (size_t)y * pw; uint8_t *o = hrows + (size_t)y * dw;
    for (int x = 0; x < dw; x++) { int k = mapX[x], fx = fracX[x]; o[x] = (uint8_t)((r[k] * (256 - fx) + r[k + 1] * fx) >> 8); } }
  // Pass 2: blend two resampled rows (streaming, vectorisable) and apply the palette.
  int y0 = dstY < 0 ? -dstY : 0, y1 = dstY + dstH > scrH ? scrH - dstY : dstH, x0 = dstX < 0 ? -dstX : 0, x1 = dstX + dw > scrW ? scrW - dstX : dw;
  for (int y = y0; y < y1; y++) {
    const uint8_t *a0 = hrows + (size_t)mapY[y] * dw, *a1 = a0 + dw; int fy = fracY[y], gy = 256 - fy;
    uint32_t *out = pixels + (size_t)(dstY + y) * scrW + dstX;
    if (cfg.highlight) { const uint8_t *c = chg + (size_t)mapY[y] * pw; for (int x = x0; x < x1; x++) out[x] = (c[mapX[x]] > 30 ? lutHot : lut)[(a0[x] * gy + a1[x] * fy) >> 8]; }
    else for (int x = x0; x < x1; x++) out[x] = lut[(a0[x] * gy + a1[x] * fy) >> 8];
  }
  tResample += qnow() - b;
overlays:
  if (cfg.pmMode == 3 && memdc && pixels == (uint32_t *)dibBits && pmLit) {
    // PM: a filled dot to the left of the hour digits at their mid-height (pattern ~(1500, 5375)); AM: nothing.
    double sc = (double)dstW / view.pw;
    int cx = dstX + (int)(((1500 - view.x) >> view.z) * sc), cy = dstY + (int)(((5375 - view.y) >> view.z) * sc), rr = (int)(((240 >> view.z) * sc) / 2); if (rr < 4) rr = 4;
    HBRUSH br = CreateSolidBrush(RGB(cfg.cells & 255, (cfg.cells >> 8) & 255, (cfg.cells >> 16) & 255)); HGDIOBJ ob = SelectObject(memdc, br), op = SelectObject(memdc, GetStockObject(NULL_PEN));
    GdiFlush(); Ellipse(memdc, cx - rr, cy - rr, cx + rr, cy + rr); SelectObject(memdc, ob); SelectObject(memdc, op); DeleteObject(br);
  }
  if (cfg.pmMode == 2 && memdc && pixels == (uint32_t *)dibBits) {
    // Label position: the machine's own label area, in screen pixels.
    double sc = (double)dstW / view.pw;
    int cx = dstX + (int)((((760 + 1280) / 2 - view.x) >> view.z) * sc), cy = dstY + (int)((((4300 + 4600) / 2 - view.y) >> view.z) * sc);
    int fh = (int)(((4600 - 4300) >> view.z) * sc * 1.6); if (fh < 12) fh = 12;
    if (!pmFont || pmFontH != fh) { if (pmFont) DeleteObject(pmFont); pmFont = CreateFontA(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"); pmFontH = fh; }
    HGDIOBJ old = SelectObject(memdc, pmFont); SetBkMode(memdc, TRANSPARENT); SetTextColor(memdc, RGB(cfg.cells & 255, (cfg.cells >> 8) & 255, (cfg.cells >> 16) & 255)); SetTextAlign(memdc, TA_CENTER | TA_BASELINE);
    GdiFlush(); TextOutA(memdc, cx, cy + fh / 3, pmLit ? "PM" : "AM", 2); SelectObject(memdc, old);
  }
}
static int write_bmp(const char *path) {
  FILE *f = fopen(path, "wb"); if (!f) return 0;
  BITMAPFILEHEADER fh = { 0x4d42, (DWORD)(54 + (size_t)scrW * scrH * 4), 0, 0, 54 }; BITMAPINFOHEADER ih = { 40, scrW, -scrH, 1, 32, 0, 0, 0, 0, 0, 0 };
  fwrite(&fh, 14, 1, f); fwrite(&ih, 40, 1, f); fwrite(pixels, 4, (size_t)scrW * scrH, f); fclose(f); return 1;
}

// ---- desktop window ------------------------------------------------------------------
static HWND hwnd, hostParent; static int g_monX, g_monY; static HBITMAP dib;
static HWND workerw_cb_result;
static BOOL CALLBACK find_workerw(HWND h, LPARAM lp) { (void)lp; if (FindWindowExA(h, NULL, "SHELLDLL_DefView", NULL)) { workerw_cb_result = FindWindowExA(NULL, h, "WorkerW", NULL); return FALSE; } return TRUE; }
// Does the screen actually show our pixels? Compare a patch at the digits'
// centre with what we drew. Only meaningful while the desktop is visible.
static int screen_shows_us(void) {
  double sc = (double)dstW / view.pw;
  int cx = dstX + (int)(((5900 - view.x) >> view.z) * sc), cy = dstY + (int)(((5375 - view.y) >> view.z) * sc), R = 48;
  int x0 = cx - R, y0 = cy - R; if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x0 + 2 * R > scrW) x0 = scrW - 2 * R; if (y0 + 2 * R > scrH) y0 = scrH - 2 * R;
  { // Is the desktop what is on screen at the sample point? If some window covers it, the check means nothing.
    HWND at = GetAncestor(WindowFromPoint((POINT){ g_monX + cx, g_monY + cy }), GA_ROOT); char cls[64] = ""; if (at) GetClassNameA(at, cls, sizeof cls);
    if (strcmp(cls, "Progman") && strcmp(cls, "WorkerW")) { logmsg("screen check skipped: %s is at the sample point", cls[0] ? cls : "nothing"); return -1; } }
  HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc); BITMAPINFO bi = { 0 }; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2 * R; bi.bmiHeader.biHeight = -2 * R; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
  uint32_t *cap; HBITMAP bm = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&cap, NULL, 0); SelectObject(mdc, bm);
  BitBlt(mdc, 0, 0, 2 * R, 2 * R, sdc, g_monX + x0, g_monY + y0, SRCCOPY | CAPTUREBLT); GdiFlush();
  int same = 0, total = 4 * R * R;
  for (int y = 0; y < 2 * R; y++) for (int x = 0; x < 2 * R; x++) if ((cap[y * 2 * R + x] & 0xffffff) == (pixels[(size_t)(y0 + y) * scrW + x0 + x] & 0xffffff)) same++;
  DeleteObject(bm); DeleteDC(mdc); ReleaseDC(NULL, sdc);
  logmsg("screen check at (%d,%d): %d%% of the patch matches", x0, y0, same * 100 / total);
  return same * 10 >= total * 7;
}
static const int ATTACH_ORDER[] = { 7, 1, 2, 3, 5 };
static void attach_to_desktop(void) {
  HWND progman = FindWindowA("Progman", NULL);
  DWORD_PTR res; SendMessageTimeoutA(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &res);
  workerw_cb_result = NULL; EnumWindows(find_workerw, 0);
  int raised = (GetWindowLongA(progman, GWL_EXSTYLE) & 0x00200000L /* WS_EX_NOREDIRECTIONBITMAP */) != 0;
  if (workerw_cb_result) { RECT wr; GetWindowRect(workerw_cb_result, &wr); if (wr.right - wr.left < scrW / 2 || wr.bottom - wr.top < scrH / 2) { logmsg("ignoring undersized WorkerW %p", (void *)workerw_cb_result); workerw_cb_result = NULL; } }
  LONG style = GetWindowLongA(hwnd, GWL_STYLE); SetWindowLongA(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
  if (workerw_cb_result && !raised) { SetParent(hwnd, workerw_cb_result); hostParent = workerw_cb_result; logmsg("attached under WorkerW %p (legacy layout)", (void *)workerw_cb_result); }
  else if (cfg.attach == 2 || cfg.attach == 3) { // child of Progman's own WorkerW (the wallpaper surface)
    HWND wallpaperW = FindWindowExA(progman, NULL, "WorkerW", NULL);
    if (cfg.attach == 3) { SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED); SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA); }
    SetParent(hwnd, wallpaperW ? wallpaperW : progman);
    logmsg("attach %d: child of Progman's WorkerW %p", cfg.attach, (void *)wallpaperW);
  }
  else if (cfg.attach == 7 || cfg.attach == 8) {
    // Windows 11 24H2/25H2 "raised desktop": Progman has WS_EX_NOREDIRECTIONBITMAP and
    // its HWND children are not composed, but SHELLDLL_DefView is a layered window
    // whose surface (with the icon list inside it) is. A child placed at the bottom
    // of DefView sits beneath the icons and is composed with per-pixel alpha, so
    // our pixels must be opaque (alpha 0xFF) or the blend turns additive.
    HWND defview = FindWindowExA(progman, NULL, "SHELLDLL_DefView", NULL);
    if (!defview) { logmsg("attach %d: no SHELLDLL_DefView under Progman", cfg.attach); return; }
    SetParent(hwnd, defview); hostParent = defview;
    SetWindowPos(hwnd, cfg.attach == 7 ? HWND_BOTTOM : HWND_TOP, g_monX, g_monY, scrW, scrH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    logmsg("attach %d: child of SHELLDLL_DefView %p", cfg.attach, (void *)defview); return;
  }
  else if (cfg.attach == 6) { // diagnostic: ordinary topmost window
    SetWindowLongA(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, scrW, scrH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    logmsg("attach 6: topmost diagnostic window"); return;
  }
  else if (cfg.attach == 5) { // plain top-level window kept at the bottom of the z-order
    SetWindowLongA(hwnd, GWL_STYLE, style); // keep WS_POPUP
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, scrW, scrH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    logmsg("attach 5: top-level window at HWND_BOTTOM"); return;
  }
  else if (cfg.attach == 4) { // diagnostic: layered child of Progman ABOVE DefView
    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED); SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    SetParent(hwnd, progman); SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    logmsg("attach 4: layered child of Progman on top");
  }
  else { // Windows 11 24H2+: Progman has WS_EX_NOREDIRECTIONBITMAP and the desktop is
    // composed by DWM; SHELLDLL_DefView and the wallpaper WorkerW are its children.
    // Our window must be a layered child (its own DWM surface) or it is never
    // composed, placed beneath DefView, with the WorkerW pushed to the bottom.
    HWND defview = FindWindowExA(progman, NULL, "SHELLDLL_DefView", NULL);
    HWND wallpaperW = FindWindowExA(progman, NULL, "WorkerW", NULL);
    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    SetParent(hwnd, progman);
    if (defview) SetWindowPos(hwnd, defview, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (wallpaperW) SetWindowPos(wallpaperW, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    logmsg("attached under Progman (24H2 layout), defview %p, workerw %p", (void *)defview, (void *)wallpaperW);
  }
  SetWindowPos(hwnd, NULL, 0, 0, scrW, scrH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
static void present(void) {
  double a = qnow();
  if (dibBits != (void *)pixels) memcpy(dibBits, pixels, (size_t)scrW * scrH * 4);
  HDC dc = GetDC(hwnd);
  // The letterbox borders never change: blit the full frame once, then only the machine's rectangle.
  int bx = dstX < 0 ? 0 : dstX, by = dstY < 0 ? 0 : dstY, bw = (dstX + dstW > scrW ? scrW : dstX + dstW) - bx, bh = (dstY + dstH > scrH ? scrH : dstY + dstH) - by;
  BOOL ok = dibPainted ? BitBlt(dc, bx, by, bw, bh, memdc, bx, by, SRCCOPY) : BitBlt(dc, 0, 0, scrW, scrH, memdc, 0, 0, SRCCOPY);
  dibPainted = 1;
  static int logged = 0; if (!logged) { logged = 1; RECT cr; GetClientRect(hwnd, &cr); logmsg("present: dc %p bitblt %d err %lu client %ldx%ld visible %d", (void *)dc, ok, GetLastError(), cr.right, cr.bottom, IsWindowVisible(hwnd)); }
  if (cfg.status) { static char line[256]; HlStats s = hl_stats(); snprintf(line, sizeof line, "gen %lld  target %lld  nodes %d  memo %u", (long long)uni.generation, (long long)target_generation(), s.nodes, s.memo);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(107, 114, 128)); TextOutA(dc, 12, scrH - 24, line, (int)strlen(line)); }
  ReleaseDC(hwnd, dc);
  tPresent += qnow() - a;
}
static volatile int paused_lock = 0, paused_display = 0, paused_manual = 0, displayChanged = 0;
static void create_dib(int W, int H) {
  if (dib) { DeleteObject(dib); dib = NULL; }
  if (!memdc) { HDC sdc = GetDC(NULL); memdc = CreateCompatibleDC(sdc); ReleaseDC(NULL, sdc); }
  BITMAPINFO bi = { 0 }; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  dib = CreateDIBSection(memdc, &bi, DIB_RGB_COLORS, &dibBits, NULL, 0); SelectObject(memdc, dib);
  if (pixels && pixels != (uint32_t *)dibBits) free(pixels); pixels = (uint32_t *)dibBits;
  uint32_t bgpx = lut[0]; for (size_t i = 0; i < (size_t)W * H; i++) pixels[i] = bgpx; dibPainted = 0;
}
#define WM_TRAY (WM_APP + 1)
#define ID_PAUSE 1
#define ID_WATCH 2
#define ID_SETTINGS 3
#define ID_LOG 4
#define ID_QUIT 5
#define ID_STARTUP 6
static NOTIFYICONDATAA nid; static HICON trayIcon;
static HICON make_icon(void) { // a 32x32 amber disc with a dark colon, drawn at runtime
  HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc); BITMAPINFO bi = { 0 }; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 32; bi.bmiHeader.biHeight = -32; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
  uint32_t *px; HBITMAP color = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0); ReleaseDC(NULL, sdc);
  uint32_t fg = 0xff000000u | ((cfg.cells & 255) << 16) | (cfg.cells & 0xff00) | ((cfg.cells >> 16) & 255), bg = 0xff000000u | ((cfg.bg & 255) << 16) | (cfg.bg & 0xff00) | ((cfg.bg >> 16) & 255);
  for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { double d = hypot(x - 15.5, y - 15.5); uint32_t c = d <= 15 ? fg : 0; if (d <= 15 && ((x >= 13 && x <= 18 && ((y >= 8 && y <= 12) || (y >= 19 && y <= 23))))) c = bg; px[y * 32 + x] = c; }
  HBITMAP mask = CreateBitmap(32, 32, 1, 1, NULL); ICONINFO ii = { TRUE, 0, 0, mask, color }; HICON h = CreateIconIndirect(&ii); DeleteObject(mask); DeleteObject(color); DeleteDC(mdc); return h;
}
static void tray_add(HWND owner) {
  trayIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1)); if (!trayIcon) trayIcon = make_icon(); memset(&nid, 0, sizeof nid); nid.cbSize = sizeof nid; nid.hWnd = owner; nid.uID = 1; nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; nid.uCallbackMessage = WM_TRAY; nid.hIcon = trayIcon; strcpy(nid.szTip, "Life Clock");
  Shell_NotifyIconA(NIM_ADD, &nid);
}
static void tray_remove(void) { Shell_NotifyIconA(NIM_DELETE, &nid); if (trayIcon) DestroyIcon(trayIcon); }
static char exeDir[MAX_PATH], exePath[MAX_PATH];
static void startup_link_path(char *out, size_t n) { char base[MAX_PATH]; SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, base); snprintf(out, n, "%s\\Life Clock.lnk", base); }
static int startup_installed(void) { char p[MAX_PATH]; startup_link_path(p, sizeof p); return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
static int startup_install(int on) {
  char p[MAX_PATH]; startup_link_path(p, sizeof p);
  if (!on) { int ok = DeleteFileA(p) || GetLastError() == ERROR_FILE_NOT_FOUND; logmsg("startup shortcut removed: %d", ok); return ok; }
  CoInitialize(NULL); IShellLinkA *sl = NULL; IPersistFile *pf = NULL; int ok = 0;
  if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&sl))) {
    sl->lpVtbl->SetPath(sl, exePath); sl->lpVtbl->SetWorkingDirectory(sl, exeDir); sl->lpVtbl->SetDescription(sl, "Life Clock wallpaper");
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) { wchar_t wp[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, p, -1, wp, MAX_PATH); ok = SUCCEEDED(pf->lpVtbl->Save(pf, wp, TRUE)); pf->lpVtbl->Release(pf); }
    sl->lpVtbl->Release(sl); }
  CoUninitialize(); logmsg("startup shortcut written: %d (%s)", ok, p); return ok;
}
static void tray_menu(HWND owner) {
  HMENU m = CreatePopupMenu();
  AppendMenuA(m, MF_STRING, ID_PAUSE, paused_manual ? "Resume" : "Pause");
  AppendMenuA(m, MF_STRING, ID_WATCH, "Watch full screen");
  AppendMenuA(m, MF_SEPARATOR, 0, NULL);
  AppendMenuA(m, MF_STRING, ID_SETTINGS, "Settings...");
  AppendMenuA(m, MF_STRING, ID_LOG, "Open log");
  AppendMenuA(m, MF_STRING | (startup_installed() ? MF_CHECKED : 0), ID_STARTUP, "Start with Windows");
  AppendMenuA(m, MF_SEPARATOR, 0, NULL);
  AppendMenuA(m, MF_STRING, ID_QUIT, "Quit");
  POINT pt; GetCursorPos(&pt); SetForegroundWindow(owner);
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, owner, NULL); DestroyMenu(m);
  if (cmd == ID_PAUSE) { paused_manual = !paused_manual; logmsg(paused_manual ? "paused from tray" : "resumed from tray"); }
  else if (cmd == ID_WATCH) ShellExecuteA(NULL, "open", exePath, "--fullscreen 1", exeDir, SW_SHOWNORMAL);
  else if (cmd == ID_SETTINGS) ShellExecuteA(NULL, "open", exePath, "--settings", exeDir, SW_SHOWNORMAL);
  else if (cmd == ID_LOG) ShellExecuteA(NULL, "open", logPath, NULL, exeDir, SW_SHOWNORMAL);
  else if (cmd == ID_STARTUP) startup_install(!startup_installed());
  else if (cmd == ID_QUIT) PostQuitMessage(0);
}
// The tray icon needs a message window of its own: the wallpaper window lives
// inside the shell view and cannot own popups reliably.
static LRESULT CALLBACK trayproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_TRAY) { if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) tray_menu(h); return 0; }
  if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
  return DefWindowProcA(h, m, w, l);
}
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); BitBlt(dc, 0, 0, scrW, scrH, memdc, 0, 0, SRCCOPY); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: BitBlt((HDC)w, 0, 0, scrW, scrH, memdc, 0, 0, SRCCOPY); return 0;
    case WM_WTSSESSION_CHANGE: if (w == WTS_SESSION_LOCK) paused_lock = 1; else if (w == WTS_SESSION_UNLOCK) paused_lock = 0; return 0;
    case WM_POWERBROADCAST: if (w == PBT_POWERSETTINGCHANGE) { POWERBROADCAST_SETTING *s = (POWERBROADCAST_SETTING *)l; if (s->DataLength >= 4) paused_display = (*(DWORD *)s->Data == 0); } return TRUE;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_DISPLAYCHANGE: case 0x02E0 /* WM_DPICHANGED */: displayChanged = 1; return 0;
    case WM_MOUSEMOVE: if (cfg.screensaver) { static POINT first = { -1, -1 }; POINT pt; GetCursorPos(&pt); if (first.x < 0) first = pt; else if (abs(pt.x - first.x) > 12 || abs(pt.y - first.y) > 12) PostQuitMessage(0); } return 0;
    case WM_KEYDOWN: if (cfg.screensaver) { PostQuitMessage(0); return 0; } if (!cfg.fullscreen) return 0;
      if (w == VK_ESCAPE || w == 'Q') PostQuitMessage(0);
      else if (w == 'T') { cfg.tour = cfg.tour > 0 ? 0 : 1; tourStart = GetTickCount(); }
      else if (w == VK_ADD || w == VK_OEM_PLUS || w == VK_UP) { if (cfg.zoom > 1) { cfg.zoom /= 2; setup_view(scrW, scrH); } }
      else if (w == VK_SUBTRACT || w == VK_OEM_MINUS || w == VK_DOWN) { if (cfg.zoom < 16) { cfg.zoom *= 2; setup_view(scrW, scrH); } }
      else if (w == VK_LEFT) { cfg.hpos += 0.1; setup_view(scrW, scrH); } else if (w == VK_RIGHT) { cfg.hpos -= 0.1; setup_view(scrW, scrH); }
      else if (w == VK_PRIOR) { cfg.vpos += 0.1; setup_view(scrW, scrH); } else if (w == VK_NEXT) { cfg.vpos -= 0.1; setup_view(scrW, scrH); }
      dibPainted = 0; render(); present(); return 0;
    case WM_LBUTTONUP: if (cfg.fullscreen) PostQuitMessage(0); return 0;
  }
  return DefWindowProcA(h, m, w, l);
}
// Is the primary monitor fully covered by other windows? (Chromium's approach:
// walk top-level windows in z-order, subtract visible, uncloaked, non-minimized ones.)
static HRGN occl_region; static RECT occl_screen; static char occl_dbg[512]; static int occl_dbg_n;
static BOOL CALLBACK occl_cb(HWND h, LPARAM lp) {
  (void)lp; if (h == hwnd || !IsWindowVisible(h) || IsIconic(h)) return TRUE;
  char cls[64]; GetClassNameA(h, cls, sizeof cls);
  // EnumWindows walks top to bottom. Reaching the desktop (Progman) means every
  // window above it has been subtracted; whatever region is left is visible
  // desktop. On the 24H2+ "raised desktop", Show Desktop lifts Progman above
  // the apps instead of minimising them, so it is met first and nothing is
  // subtracted at all.
  if (!strcmp(cls, "Progman")) { size_t l = strlen(occl_dbg); snprintf(occl_dbg + l, sizeof occl_dbg - l, " [reached Progman]"); return FALSE; }
  if (!strcmp(cls, "WorkerW")) return TRUE;
  int cloaked = 0; DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked); if (cloaked) return TRUE;
  LONG ex = GetWindowLongA(h, GWL_EXSTYLE);
  if (ex & WS_EX_TRANSPARENT) return TRUE;                       // click-through overlays (game overlays, etc.) do not hide the desktop
  if (ex & WS_EX_LAYERED) { BYTE alpha; DWORD flags;             // translucent or per-pixel-alpha windows: treat as not occluding
    if (!GetLayeredWindowAttributes(h, NULL, &alpha, &flags) || ((flags & LWA_ALPHA) && alpha < 250) || (flags & LWA_COLORKEY)) return TRUE; }
  RECT r; if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(h, &r);
  if (r.right - r.left < 8 || r.bottom - r.top < 8) return TRUE;   // zero-size helper windows
  HRGN wr = CreateRectRgnIndirect(&r); CombineRgn(occl_region, occl_region, wr, RGN_DIFF); DeleteObject(wr);
  if (occl_dbg_n < 10) { size_t l = strlen(occl_dbg); snprintf(occl_dbg + l, sizeof occl_dbg - l, " [%s %ldx%ld]", cls, r.right - r.left, r.bottom - r.top); occl_dbg_n++; }
  RECT box; return GetRgnBox(occl_region, &box) != NULLREGION;
}
static int desktop_covered(void) {
  occl_screen.left = 0; occl_screen.top = 0; occl_screen.right = scrW; occl_screen.bottom = scrH;
  occl_region = CreateRectRgnIndirect(&occl_screen); occl_dbg[0] = 0; occl_dbg_n = 0; EnumWindows(occl_cb, 0);
  RECT box; int empty = GetRgnBox(occl_region, &box) == NULLREGION; DeleteObject(occl_region);
  QUERY_USER_NOTIFICATION_STATE q; if (SUCCEEDED(SHQueryUserNotificationState(&q)) && (q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_BUSY)) return 1;
  return empty;
}

// ---- main -------------------------------------------------------------------
// ---- settings: ini file + command line ---------------------------------------
static const char *PRESETS[][3] = { { "amber", "07090f", "ffe9a8" }, { "green", "040a06", "5cff7a" }, { "white", "0a0a0a", "f2f2f2" }, { "blue", "070a14", "8cd2ff" }, { "red", "0c0605", "ff6b57" } };
static DWORD parse_hex_bgr(const char *s) { unsigned v = (unsigned)strtoul(s[0] == '#' ? s + 1 : s, NULL, 16); return ((v & 0xff) << 16) | (v & 0xff00) | ((v >> 16) & 0xff); }
static void apply_setting(const char *key, const char *val) {
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
  else if (!strcmp(key, "zoom")) cfg.zoom = !strcmp(val, "auto") ? 0 : atoi(val);
  else if (!strcmp(key, "fullscreen")) cfg.fullscreen = atoi(val) || !strcmp(val, "true");
  else if (!strcmp(key, "colon")) cfg.colonMode = !strcmp(val, "machine") ? 0 : !strcmp(val, "hide") ? 2 : 1; // 1 = pulse (default)
  else if (!strcmp(key, "palette")) { strncpy(cfg.palette, val, 15); for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) if (!strcmp(val, PRESETS[i][0])) { cfg.bg = parse_hex_bgr(PRESETS[i][1]); cfg.cells = parse_hex_bgr(PRESETS[i][2]); cfg.hasCells2 = 0; } }
  else if (!strcmp(key, "bg")) cfg.bg = parse_hex_bgr(val);
  else if (!strcmp(key, "cells")) cfg.cells = parse_hex_bgr(val);
  else if (!strcmp(key, "cells2")) { if (val[0] && strcmp(val, "none")) { cfg.cells2 = parse_hex_bgr(val); cfg.hasCells2 = 1; } else cfg.hasCells2 = 0; }
}
static void clamp_settings(void) {
  if (cfg.fps != 3 && cfg.fps != 6 && cfg.fps != 12 && cfg.fps != 24) cfg.fps = 6;
  if (cfg.batteryFps != 1 && cfg.batteryFps != 3 && cfg.batteryFps != 6 && cfg.batteryFps != 12 && cfg.batteryFps != 24) cfg.batteryFps = 3;
  if (cfg.gain < 1) cfg.gain = 1; if (cfg.gain > 255) cfg.gain = 255;
  if (cfg.afterglow < 0) cfg.afterglow = 0; if (cfg.afterglow > 0.95) cfg.afterglow = 0.95;
  if (cfg.zoom != 0 && cfg.zoom != 1 && cfg.zoom != 2 && cfg.zoom != 4 && cfg.zoom != 8 && cfg.zoom != 16) cfg.zoom = 0;
  if (cfg.size < 0.3) cfg.size = 0.3; if (cfg.size > 2.0) cfg.size = 2.0;
  if (cfg.hpos < 0.0) cfg.hpos = 0.0; if (cfg.hpos > 1.0) cfg.hpos = 1.0;
  if (cfg.vpos < 0.2) cfg.vpos = 0.2; if (cfg.vpos > 0.9) cfg.vpos = 0.9;
}
static const char *INI_TEMPLATE =
"; Life Clock settings. Edit and save: the wallpaper reloads this file within 2 s.\n"
"; Nothing here costs CPU while running; colours and layout are applied once per change.\n"
"\n"
"; Frames per second: 3, 6, 12 or 24 (generations per frame = 192 / fps). Lower = less CPU.\n"
"fps = 6\n"
"; Frame rate while on battery: 1, 3, 6, 12 or 24.\n"
"battery_fps = 3\n"
"\n"
"; whole = the machine (top cropped depending on vpos), display = just the 7-segment display.\n"
"view = whole\n"
"; Size relative to the largest fit (1.0 = fills the screen width; >1 crops; 0.5 = half).\n"
"size = 1.0\n"
"; Where the digits' centre sits: fraction of the screen width / height.\n"
"hpos = 0.5\n"
"vpos = 0.5\n"
"; Monitor index (0 = primary).\n"
"monitor = 0\n"
"\n"
"; Colours. palette = amber | green | white | blue | red, or set bg / cells / cells2 yourself\n"
"; (RRGGBB). cells2, if set, is the colour of the densest areas (two-tone ramp); 'none' disables.\n"
"palette = amber\n"
"; bg = 07090f\n"
"; cells = ffe9a8\n"
"cells2 = none\n"
"; Brightness of one live cell in a zoomed-out pixel (5-120).\n"
"gain = 40\n"
"\n"
"; AM/PM: the machine has a box that is an outline in the morning and filled in the afternoon,\n"
"; next to a static 'PM' label. dot = a filled dot beside the digits when PM, nothing when AM,\n"
"; read from that box (default); text = keep the box and write AM/PM; machine = as drawn; hide.\n"
"pm = dot\n"
"; The colon: pulse = the pattern's still-life discs replaced by discs of pulsars (period-3\n"
"; oscillators) so the dots breathe under Life's rules (default); machine = as drawn; hide.\n"
"colon = pulse\n"
"\n"
"; Highlight: 1 = colour cells that changed since the last frame in 'hot' (default c8e9ff), so the\n"
"; working parts of the machine stand out from the static hardware; 0 = off.\n"
"highlight = 0\n"
"; hot = c8e9ff\n"
"\n"
"; Tour: in watch mode and the screensaver, pan slowly around the machine (timebase, distribution,\n"
"; lookup tables, digits). auto = on for the screensaver, off for watch mode; 1 = on; 0 = off.\n"
"tour = auto\n"
"\n"
"; Afterglow: 0 = off; 0.5-0.9 leaves fading trails behind moving cells (one cheap pass per frame).\n"
"afterglow = 0\n"
"\n"
"; Zoom: auto fits the view to the screen (1/8 for the whole machine); 8, 4, 2 or 1 fix\n"
"; cells per pixel, centred on the digits (hpos/vpos still apply). 4 shows individual gliders.\n"
"zoom = auto\n"
"\n"
"; Small stats line in the corner: 0 or 1.\n"
"status = 0\n";
static void load_ini(void) {
  FILE *f = fopen(iniPath, "r");
  if (!f) { f = fopen(iniPath, "w"); if (f) { fputs(INI_TEMPLATE, f); fclose(f); logmsg("wrote default %s", iniPath); } return; }
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char *p = line; while (*p == ' ' || *p == '\t') p++; if (*p == ';' || *p == '#' || *p == '\n' || *p == 0) continue;
    char *eq = strchr(p, '='); if (!eq) continue; *eq = 0; char *k = p, *v = eq + 1;
    char *e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    while (*v == ' ' || *v == '\t') v++; e = v + strlen(v); while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    char *c = strchr(v, ';'); if (c) { *c = 0; e = v + strlen(v); while (e > v && e[-1] == ' ') *--e = 0; }
    apply_setting(k, v);
  }
  fclose(f);
}
static void apply_args(void) {
  for (int i = 1; i < g_argc; i++) if (g_argv[i][0] == '-' && g_argv[i][1] == '-' && i + 1 < g_argc && g_argv[i + 1][0] != '-') { apply_setting(g_argv[i] + 2, g_argv[i + 1]); i++; }
    else if (!strcmp(g_argv[i], "--status")) cfg.status = 1;
}
static int ini_changed(void) {
  WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExA(iniPath, GetFileExInfoStandard, &a)) return 0;
  if (CompareFileTime(&a.ftLastWriteTime, &iniTime) == 0) return 0; iniTime = a.ftLastWriteTime; return 1;
}
static void load_settings(void) { cfg_defaults(); load_ini(); apply_args(); clamp_settings(); }

static void pick_monitor(int *mx, int *my, int *w, int *h) {
  RECT mons[8]; int nm = 0; BOOL CALLBACK monproc(HMONITOR, HDC, LPRECT, LPARAM);
  HMONITOR prim = MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY); MONITORINFO mi = { sizeof mi }; GetMonitorInfoA(prim, &mi); mons[nm++] = mi.rcMonitor;
  EnumDisplayMonitors(NULL, NULL, monproc, (LPARAM)&(struct { RECT *r; int *n; HMONITOR prim; }){ mons, &nm, prim });
  int idx = cfg.monitor; if (idx < 0 || idx >= nm) idx = 0;
  *mx = mons[idx].left; *my = mons[idx].top; *w = mons[idx].right - mons[idx].left; *h = mons[idx].bottom - mons[idx].top;
  logmsg("monitors: %d; using %d at (%d,%d) %dx%d", nm, idx, *mx, *my, *w, *h);
}
BOOL CALLBACK monproc(HMONITOR h, HDC dc, LPRECT r, LPARAM lp) { (void)dc; (void)r; struct { RECT *r; int *n; HMONITOR prim; } *ctx = (void *)lp; if (h == ctx->prim || *ctx->n >= 8) return TRUE; MONITORINFO mi = { sizeof mi }; GetMonitorInfoA(h, &mi); ctx->r[(*ctx->n)++] = mi.rcMonitor; return TRUE; }
static volatile int quitRequested;
static void pump(void) { MSG m; while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) quitRequested = 1; TranslateMessage(&m); DispatchMessageA(&m); } }

int main(int argc, char **argv) {
  g_argc = argc; g_argv = argv;
  int scrCfg = 0, scrPreview = 0, scrRun = 0;
  for (int i = 1; i < argc; i++) { // Windows screensaver conventions: /s run, /c configure, /p preview
    const char *a = argv[i]; if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && (a[2] == 0 || a[2] == ':')) scrRun = 1;
    if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (a[2] == 0 || a[2] == ':')) scrCfg = 1;
    if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (a[2] == 0 || a[2] == ':')) scrPreview = 1; }
  if (scrPreview) return 0;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--install-startup") || !strcmp(argv[i], "--uninstall-startup")) {
    GetModuleFileNameA(NULL, exePath, MAX_PATH); strcpy(exeDir, exePath); char *sl = strrchr(exeDir, '\\'); if (sl) sl[1] = 0;
    return startup_install(!strcmp(argv[i], "--install-startup")) ? 0 : 1; }
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frame") && i + 1 < argc) cfg.frame = argv[i + 1];
    else if (!strcmp(argv[i], "--quit")) { int fs = 0; for (int j = 1; j < argc; j++) if (!strcmp(argv[j], "--fullscreen")) fs = 1;
      HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, fs ? "LifeClockFullscreenQuit" : "LifeClockWallpaperQuit"); if (ev) { SetEvent(ev); CloseHandle(ev); return 0; } return 1; }
  }
  const char *frameArg = cfg.frame;
  char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH); strcpy(exePath, exe); char *slash = strrchr(exe, '\\'); if (slash) slash[1] = 0; strcpy(exeDir, exe);
  snprintf(logPath, sizeof logPath, "%slife-clock.log", exe);
  { // at startup, a log last written on an earlier day is rotated away first
    WIN32_FILE_ATTRIBUTE_DATA a; SYSTEMTIME lw, now; GetLocalTime(&now);
    if (GetFileAttributesExA(logPath, GetFileExInfoStandard, &a)) { FILETIME lt; FileTimeToLocalFileTime(&a.ftLastWriteTime, &lt); FileTimeToSystemTime(&lt, &lw);
      if (lw.wDay != now.wDay || lw.wMonth != now.wMonth || lw.wYear != now.wYear) { char prev[MAX_PATH]; snprintf(prev, sizeof prev, "%slife-clock.prev.log", exe); MoveFileExA(logPath, prev, MOVEFILE_REPLACE_EXISTING); } }
    logDay = now.wDay; }
  logfile = fopen(logPath, "a");
  snprintf(iniPath, sizeof iniPath, "%slife-clock.ini", exe);
  load_settings(); cfg.frame = frameArg; ini_changed();
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--settings")) scrCfg = 1;
  if (scrCfg) return settings_main(iniPath, exePath, exeDir);
  if (scrRun) { cfg.fullscreen = 1; cfg.screensaver = 1; }
  logmsg("start: fps %d/%d view %d size %.2f pos %.2f,%.2f palette %s gain %d frame %s", cfg.fps, cfg.batteryFps, cfg.view, cfg.size, cfg.hpos, cfg.vpos, cfg.palette, cfg.gain, cfg.frame ? cfg.frame : "-");

  HANDLE mutex = NULL;
  if (!cfg.frame) { mutex = CreateMutexA(NULL, TRUE, cfg.fullscreen ? "LifeClockFullscreen" : "LifeClockWallpaper"); if (GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("already running"); return 2; } }
  HANDLE quitEv = CreateEventA(NULL, TRUE, FALSE, cfg.fullscreen ? "LifeClockFullscreenQuit" : "LifeClockWallpaperQuit");
  if (cfg.fullscreen && !cfg.zoom) cfg.zoom = 4;   // watch mode: gliders visible by default

  typedef BOOL (WINAPI *SetCtx)(HANDLE); SetCtx setctx = (SetCtx)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
  BOOL dpiok = setctx ? setctx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */) : SetProcessDPIAware();
  DWORD dpierr = GetLastError();
  int W, H, monX, monY; pick_monitor(&monX, &monY, &W, &H); g_monX = monX; g_monY = monY;
  { DEVMODEA dm = { 0 }; dm.dmSize = sizeof dm; EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm);
    typedef UINT (WINAPI *GDFS)(void); GDFS gdfs = (GDFS)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    logmsg("dpi: awareness call ok=%d err=%lu, metrics %dx%d, display mode %lux%lu, system dpi %u", dpiok, dpierr, W, H, dm.dmPelsWidth, dm.dmPelsHeight, gdfs ? gdfs() : 0); }

  QueryPerformanceFrequency(&qpf); hl_init(); build_palette();
  LARGE_INTEGER freq, t0, t1; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
  if (!load_snapshot(current_minute())) return 3;
  setup_view(W, H);

  if (cfg.frame) { // headless: sync, render into a memory DIB (so text overlays work), dump
    HDC sdc = GetDC(NULL); memdc = CreateCompatibleDC(sdc); ReleaseDC(NULL, sdc);
    BITMAPINFO bi = { 0 }; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(memdc, &bi, DIB_RGB_COLORS, &dibBits, NULL, 0); SelectObject(memdc, dib);
    free(pixels); pixels = (uint32_t *)dibBits;
    { uint32_t bgpx = lut[0]; for (size_t i = 0; i < (size_t)W * H; i++) pixels[i] = bgpx; }
    int64_t target = target_generation(); while (target - uni.generation >= GPS) { int64_t step = target - uni.generation; if (step > PERIOD) step = PERIOD; hl_advance(&uni, step); target = target_generation(); }
    QueryPerformanceCounter(&t1); logmsg("synced to gen %lld in %.1f s", (long long)uni.generation, (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart);
    render(); if (cfg.afterglow > 0 || cfg.highlight) for (int i = 0; i < 8; i++) { hl_advance(&uni, 32); render(); } GdiFlush();
    if (cfg.frames > 1) { tourStart = 0; for (int i = 0; i < cfg.frames; i++) { char fn[MAX_PATH]; snprintf(fn, sizeof fn, "%s_%03d.bmp", cfg.frame, i);
        if (cfg.tour > 0 && cfg.zoom) { double cx, cy; tour_center(i / 6.0, &cx, &cy); set_center(cx, cy); }
        hl_advance(&uni, cfg.frameStep > 0 ? cfg.frameStep : 32); render(); GdiFlush(); write_bmp(fn); } }
    else write_bmp(cfg.frame); HlStats s = hl_stats(); logmsg("frame written: %s; nodes %d memo %u tables %.0f MB", cfg.frame, s.nodes, s.memo, s.bytes / 1e6); return 0;
  }

  WNDCLASSA wc = { 0 }; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "LifeClockWallpaper"; wc.hbrBackground = NULL; wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1)); RegisterClassA(&wc);
  hwnd = cfg.fullscreen ? CreateWindowExA(0, wc.lpszClassName, "Life Clock", WS_POPUP, monX, monY, W, H, NULL, NULL, wc.hInstance, NULL)
                        : CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, "Life Clock", WS_POPUP, monX, monY, W, H, NULL, NULL, wc.hInstance, NULL);
  create_dib(W, H);
  render();
  HWND trayWnd = NULL;
  if (cfg.fullscreen) { SetWindowPos(hwnd, cfg.screensaver ? HWND_TOPMOST : HWND_TOP, monX, monY, W, H, SWP_SHOWWINDOW); SetForegroundWindow(hwnd); SetFocus(hwnd); if (cfg.screensaver) ShowCursor(FALSE); logmsg(cfg.screensaver ? "screensaver window" : "fullscreen watch window (Esc/click closes, +/- zoom, arrows pan)"); }
  else { attach_to_desktop();
    WNDCLASSA tc = { 0 }; tc.lpfnWndProc = trayproc; tc.hInstance = wc.hInstance; tc.lpszClassName = "LifeClockTray"; RegisterClassA(&tc);
    trayWnd = CreateWindowExA(0, tc.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL); tray_add(trayWnd); }
  present();
  { typedef UINT (WINAPI *GDFW)(HWND); GDFW gdfw = (GDFW)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow"); RECT wr; GetWindowRect(hwnd, &wr);
    logmsg("window: dpi %u, rect (%ld,%ld)-(%ld,%ld), parent %p", gdfw ? gdfw(hwnd) : 0, wr.left, wr.top, wr.right, wr.bottom, (void *)GetParent(hwnd)); }
  WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
  { GUID g = { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } }; /* GUID_CONSOLE_DISPLAY_STATE */ RegisterPowerSettingNotification(hwnd, &g, DEVICE_NOTIFY_WINDOW_HANDLE); }

  // Sync in slices, presenting the frozen snapshot meanwhile.
  int64_t target = target_generation();
  while (target - uni.generation >= GPS) { int64_t step = target - uni.generation; if (step > PERIOD) step = PERIOD; hl_advance(&uni, step); render(); present(); pump(); target = target_generation(); }
  QueryPerformanceCounter(&t1); logmsg("synced to gen %lld in %.1f s", (long long)uni.generation, (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart);

  int stepGens = GPS / cfg.fps; DWORD frameMs = 1000 / cfg.fps; int paused = 0; DWORD lastOccl = 0; int frames = 0; double workMs = 0; DWORD lastReport = GetTickCount(); int onBattery = 0; DWORD lastIni = 0;
  int attachVerified = cfg.fullscreen, attachTry = 0; DWORD visibleSince = 0, recheckAt = 0; int n_force = 0; tourStart = GetTickCount();
  if (cfg.tour < 0) cfg.tour = cfg.screensaver;   // auto: tour in the screensaver, static in watch mode
  for (;;) {
    DWORD r = MsgWaitForMultipleObjects(1, &quitEv, FALSE, paused ? 1000 : frameMs, QS_ALLINPUT);
    if (r == WAIT_OBJECT_0) break;
    pump(); if (quitRequested) break;
    DWORD now = GetTickCount();
    if (!cfg.fullscreen && now - lastOccl > 1000 && (!IsWindow(hwnd) || (hostParent && !IsWindow(hostParent)))) {
      // Explorer is restarting: wait until it has rebuilt the desktop (Progman with its shell view) before re-attaching.
      HWND pm = FindWindowA("Progman", NULL); if (!pm || !FindWindowExA(pm, NULL, "SHELLDLL_DefView", NULL)) { static DWORD lastWait; if (now - lastWait > 5000) { lastWait = now; logmsg("desktop window gone; waiting for Explorer to rebuild the desktop"); } lastOccl = now; continue; }
      logmsg("desktop window gone (Explorer restart?): recreating");
      if (IsWindow(hwnd)) DestroyWindow(hwnd);
      hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "LifeClockWallpaper", "Life Clock", WS_POPUP, 0, 0, scrW, scrH, NULL, NULL, GetModuleHandleA(NULL), NULL);
      hostParent = NULL; attach_to_desktop(); present(); lastOccl = now; attachVerified = 0; attachTry = 0; visibleSince = 0; continue;
    }
    if (now - lastIni > 2000) { lastIni = now;
      if (ini_changed()) { int oldColon = cfg.colonMode; load_settings(); build_palette(); setup_view(scrW, scrH); dibPainted = 0; if (cfg.colonMode != oldColon) load_snapshot(current_minute()); render(); present(); logmsg("settings reloaded: fps %d/%d view %d size %.2f pos %.2f,%.2f palette %s gain %d", cfg.fps, cfg.batteryFps, cfg.view, cfg.size, cfg.hpos, cfg.vpos, cfg.palette, cfg.gain); }
      SYSTEM_POWER_STATUS ps; GetSystemPowerStatus(&ps); onBattery = (ps.ACLineStatus == 0);
      int f = onBattery ? cfg.batteryFps : cfg.fps; stepGens = GPS / f; frameMs = 1000 / f; }
    if (now - lastOccl > 1000) { lastOccl = now;
      int p = paused_lock || paused_display || paused_manual || (!cfg.fullscreen && desktop_covered()); if (p != paused) { paused = p; if (paused) logmsg("paused (lock %d display-off %d; windows subtracted:%s)", paused_lock, paused_display, occl_dbg); else logmsg("resumed"); } }
    if (displayChanged) { displayChanged = 0; int mx, my, w, h; pick_monitor(&mx, &my, &w, &h);
      if (mx != g_monX || my != g_monY || w != scrW || h != scrH) { logmsg("display changed: %dx%d at (%d,%d)", w, h, mx, my); g_monX = mx; g_monY = my; scrW = w; scrH = h;
        create_dib(w, h); SetWindowPos(hwnd, NULL, mx, my, w, h, SWP_NOZORDER | SWP_NOACTIVATE); setup_view(w, h); render(); present(); } }
    if (paused) { visibleSince = 0; continue; }
    if (!visibleSince) visibleSince = now;
    if (recheckAt && now >= recheckAt) { recheckAt = 0; attachVerified = 0; attachTry = 0; }
    if (!attachVerified && now - visibleSince > 2500) { // the desktop has been visible for a while: is our picture on it?
      int r = screen_shows_us();
      if (r < 0) visibleSince = now;   // could not tell (something covers the sample point); ask again later
      else if (r) { attachVerified = 1; logmsg("attachment verified on screen (strategy %d)", cfg.attach); }
      else { int next = -1; while (attachTry < (int)(sizeof ATTACH_ORDER / sizeof ATTACH_ORDER[0])) { int c = ATTACH_ORDER[attachTry++]; if (c != cfg.attach) { next = c; break; } }
        if (next < 0) { // nothing verified: go back to the default and try the whole cycle again in five minutes
          attachVerified = 1; attachTry = 0; logmsg("no attachment strategy verified; returning to 7, will re-check in 5 min");
          if (cfg.attach != 7) { cfg.attach = 7; DestroyWindow(hwnd); hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "LifeClockWallpaper", "Life Clock", WS_POPUP, g_monX, g_monY, scrW, scrH, NULL, NULL, GetModuleHandleA(NULL), NULL); hostParent = NULL; attach_to_desktop(); dibPainted = 0; present(); }
          recheckAt = now + 300000; }
        else { logmsg("not visible with strategy %d; trying %d", cfg.attach, next); cfg.attach = next;
          DestroyWindow(hwnd); hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "LifeClockWallpaper", "Life Clock", WS_POPUP, g_monX, g_monY, scrW, scrH, NULL, NULL, GetModuleHandleA(NULL), NULL);
          hostParent = NULL; attach_to_desktop(); dibPainted = 0; present(); visibleSince = now; continue; } }
    }
    LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
    target = target_generation(); int64_t behind = target - uni.generation;
    if (behind < -PERIOD || behind > 30 * PERIOD) { logmsg("resync: behind %lld", (long long)behind); load_snapshot(current_minute()); continue; }
    if (cfg.fullscreen && cfg.tour > 0 && cfg.zoom) { double cx, cy; tour_center((now - tourStart) / 1000.0, &cx, &cy); set_center(cx, cy); n_force = 1; }
    int n = 0; while (behind >= stepGens && n < 8) { hl_advance(&uni, stepGens); behind -= stepGens; n++; }
    if (behind >= stepGens) { hl_advance(&uni, behind - behind % stepGens); n++; } // long pause: one jump
    if (n || n_force) { render(); present(); frames++; n_force = 0; }
    HlStats s = hl_stats(); if (s.nodes > 3000000) { HlGcResult g = hl_gc(&uni); logmsg("gc: %d -> %d nodes", g.before, g.after); }
    QueryPerformanceCounter(&b); workMs += (double)(b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart;
    if (now - lastReport > 60000) { logmsg("last minute: %d frames, work %.0f ms = %.2f%% of one core [render %.1f, resample %.1f, present %.1f ms/frame], nodes %d, memo %u, tables %.0f MB", frames, workMs, workMs / 600.0, frames ? tRender / frames : 0, frames ? tResample / frames : 0, frames ? tPresent / frames : 0, s.nodes, s.memo, s.bytes / 1e6); frames = 0; workMs = 0; tRender = tResample = tPresent = 0; lastReport = now; }
  }
  if (trayWnd) tray_remove(); logmsg("quit"); if (mutex) CloseHandle(mutex); return 0;
}
