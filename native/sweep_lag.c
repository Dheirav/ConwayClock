// Is 12,800 the right display lag?
//
// The machine spends part of every minute redrawing, so the generation the
// wallpaper runs ahead of the machine's counter decides how much of each minute
// shows the right time. DISPLAY_LAG was set to 12,800 from a sample of minutes;
// this walks the whole 24-hour cycle once, recording what the display reads at
// every 64th generation, and then scores every candidate lag against that single
// pass -- a lag only picks which of those samples the wallpaper would have shown.
//
//   gcc -O2 -w -o sweep_lag sweep_lag.c hashlife.c inflate.c snapshots_data.c
//   ./sweep_lag [progress-file]
#include "hashlife.h"
#include "inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const int SNAP_COUNT; extern const int SNAP_MINUTE[]; extern const size_t SNAP_OFF[]; extern const unsigned char SNAP_DATA[];
#define PERIOD 11520
#define GPS 192
#define CYCLE (1440LL * PERIOD)
#define STEP 64                      // 12,800 and every s*192 are multiples of 64
// Past the end of the cycle, not wrapped to the start of it. At the last minutes
// of the day the wallpaper's target generation is beyond 24 hours and it simply
// runs the machine on; it cannot wrap, because generation 0 was hand-set to
// 12:00 rather than arrived at from 11:59, so the pattern there is not a
// continuation of anything and every reading taken from it would be wrong.
#define TAIL 30000
#define NSAMP (int)((CYCLE + TAIL) / STEP)

// The digits, and the AM/PM box, as block-aligned regions so a density map at
// 1/8 sums to exact cell counts.
#define RX 2040
#define RY 4128
#define RW 7680
#define RH 2608
#define PX 96
#define PY 4144
#define PW 584
#define PH 608
#define Z 3
static uint8_t *dens, *pdens;
static const int dw = RW >> Z, dh = RH >> Z, pw = PW >> Z, ph = PH >> Z;
static Universe u;
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static int count(int px, int py, int w, int h) {
  int x0 = (px - RX) >> Z, x1 = (px + w - RX + 7) >> Z, y0 = (py - RY) >> Z, y1 = (py + h - RY + 7) >> Z;
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > dw) x1 = dw; if (y1 > dh) y1 = dh;
  int s = 0; for (int y = y0; y < y1; y++) { const uint8_t *r = dens + (size_t)y * dw; for (int x = x0; x < x1; x++) s += r[x]; }
  return s;
}
#define TH 30
static char digit(int x0, int x1) {
  int xm = (x0 + x1) / 2, s[7];
  s[0] = count(xm - 300, 4130, 600, 200); s[6] = count(xm - 300, 5330, 600, 200); s[3] = count(xm - 300, 6530, 600, 200);
  s[5] = count(x0, 4500, xm - x0, 650);   s[1] = count(xm, 4500, x1 - xm, 650);
  s[4] = count(x0, 5700, xm - x0, 600);   s[2] = count(xm, 5700, x1 - xm, 600);
  char on[8]; int k = 0; for (int i = 0; i < 7; i++) if (s[i] > TH) on[k++] = (char)('a' + i); on[k] = 0;
  static const char *pat[10] = { "abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg" };
  for (int d = 0; d < 10; d++) if (!strcmp(on, pat[d])) return (char)('0' + d);
  return '?';
}
static int pm_lit(void) {
  int s = 0; for (int y = 0; y < ph; y++) { const uint8_t *r = pdens + (size_t)y * pw; for (int x = 0; x < pw; x++) s += r[x]; }
  return s > 7400;   // outline about 4,890 cells, filled about 9,956
}
// What minute of the cycle does the display read, or -1 if it is mid-redraw?
static int read_minute(void) {
  char lead = count(2040, 4500, 360, 1800) > TH ? '1' : ' ';
  char h = digit(3080, 4720), m1 = digit(5880, 7320), m2 = digit(8200, 9720);
  if (h == '?' || m1 == '?' || m2 == '?') return -1;
  int hour = (lead == '1' ? 10 : 0) + (h - '0');
  if (hour < 1 || hour > 12) return -1;
  int mins = (m1 - '0') * 10 + (m2 - '0');
  if (mins > 59) return -1;
  int h24 = (hour % 12) + (pm_lit() ? 12 : 0);          // 12 AM -> 0, 12 PM -> 12
  return ((h24 + 12) % 24) * 60 + mins;                  // cycle minute: generation 0 is 12:00 PM
}

int main(int argc, char **argv) {
  const char *progress = (argc > 1 && strcmp(argv[1], "-")) ? argv[1] : NULL;
  int limit = argc > 2 ? atoi(argv[2]) : NSAMP;   // smoke-test with a short sweep
  hl_init();
  dens = malloc((size_t)dw * dh); pdens = malloc((size_t)pw * ph);
  size_t len; uint8_t *text = gunzip(SNAP_DATA + SNAP_OFF[0], SNAP_OFF[1] - SNAP_OFF[0], &len);
  if (!text) { fprintf(stderr, "snapshot 0 failed\n"); return 2; }
  int32_t n; Cell *cells = hl_parse_rle((const char *)text, 1, &n); free(text);
  hl_from_cells(&u, cells, n); free(cells); u.generation = 0;

  int16_t *shown = malloc(sizeof(int16_t) * NSAMP);
  const char *cache = getenv("SWEEP_CACHE");
  if (cache) { FILE *f = fopen(cache, "rb");
    if (f) { size_t got = fread(shown, sizeof(int16_t), NSAMP, f); fclose(f);
      if (got == (size_t)NSAMP) { fprintf(stderr, "loaded %d samples from %s\n", NSAMP, cache); goto analyse; }
      fprintf(stderr, "%s holds %zu of %d samples; sweeping again\n", cache, got, NSAMP); } }
  {
  double t0 = now_s(), last = t0;
  for (int i = 0; i < limit; i++) {
    if (u.generation < (int64_t)i * STEP) hl_advance(&u, (int64_t)i * STEP - u.generation);
    hl_density_cached(&u, RX, RY, dw, dh, Z, dens);
    hl_density_cached(&u, PX, PY, pw, ph, Z, pdens);
    shown[i] = (int16_t)read_minute();
    if (hl_stats().nodes > 3000000) hl_gc(&u);
    if (progress && (i % 512 == 0 || i == NSAMP - 1)) {
      double now = now_s();
      if (now - last > 0.5 || i == NSAMP - 1) { last = now;
        FILE *f = fopen(progress, "w");
        if (f) { fprintf(f, "%d %d %.1f\n", i + 1, NSAMP, now - t0); fclose(f); } }
    }
  }
  double elapsed = now_s() - t0;
  fprintf(stderr, "swept %d samples in %.0f s (%.2f ms each)\n", limit, elapsed, elapsed * 1000 / limit);
  if (limit < NSAMP) {   // smoke test: does the reader agree with test_clock at a known generation?
    for (int M = 0; M <= 5; M += 5) { int64_t g = (int64_t)M * PERIOD + 12800 + 30 * GPS;
      if (g / STEP < limit) fprintf(stderr, "minute %d at lag 12800 second 30 reads as minute %d (want %d)\n", M, shown[g / STEP], M); }
    return 0; }
  if (cache) { FILE *f = fopen(cache, "wb"); if (f) { fwrite(shown, sizeof(int16_t), NSAMP, f); fclose(f); fprintf(stderr, "wrote %s\n", cache); } }
  }
analyse:

  // Score every lag. At wall-clock minute M second s the wallpaper renders
  // generation M*PERIOD + L + s*GPS, which is sample (that / STEP).
  printf("# lag  mean seconds correct  worst minute  worst seconds\n");
  int bestMeanL = 0, bestWorstL = 0; double bestMean = -1; int bestWorst = -1;
  for (int L = 6400; L <= 24000; L += STEP) {
    long total = 0; int worst = 61, worstM = -1;
    for (int M = 0; M < 1440; M++) {
      int ok = 0;
      for (int s = 0; s < 60; s++) {
        int64_t g = (int64_t)M * PERIOD + L + (int64_t)s * GPS;
        if (g / STEP < NSAMP && shown[g / STEP] == M) ok++;
      }
      total += ok;
      if (ok < worst) { worst = ok; worstM = M; }
    }
    double mean = total / 1440.0;
    if (mean > bestMean) { bestMean = mean; bestMeanL = L; }
    if (worst > bestWorst) { bestWorst = worst; bestWorstL = L; }
    if (L % 512 == 0 || L == 12800) printf("%6d  %5.1f  %5d  %3d%s\n", L, mean, worstM, worst, L == 12800 ? "   <- current" : "");
  }
  printf("\nbest mean:  lag %d, %.1f s of every minute correct\n", bestMeanL, bestMean);
  printf("best worst: lag %d, worst minute correct for %d s\n", bestWorstL, bestWorst);
  // The distribution at the shipping lag, which is what the documentation claims.
  { int secs[1440]; int hist[7] = {0};
    for (int M = 0; M < 1440; M++) { int ok = 0;
      for (int s = 0; s < 60; s++) { int64_t g = (int64_t)M * PERIOD + 12800 + (int64_t)s * GPS; if (g / STEP < NSAMP && shown[g / STEP] == M) ok++; }
      secs[M] = ok; hist[ok >= 60 ? 6 : ok / 10]++; }
    printf("\nseconds correct per minute at lag 12800, over all 1440 minutes:\n");
    static const char *band[6] = { " 0-9", "10-19", "20-29", "30-39", "40-49", "50-59" };
    for (int i = 0; i < 6; i++) printf("  %s s: %4d minutes (%.1f%%)\n", band[i], hist[i], 100.0 * hist[i] / 1440);
    int order[1440]; for (int i = 0; i < 1440; i++) order[i] = i;
    for (int i = 0; i < 1440; i++) for (int j = i + 1; j < 1440; j++) if (secs[order[j]] < secs[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    printf("  worst ten minutes:");
    for (int i = 0; i < 10; i++) { int M = order[i], h = (M / 60 + 12) % 24; printf(" %02d:%02d(%ds)", h, M % 60, secs[M]); }
    printf("\n  best ten:");
    for (int i = 0; i < 10; i++) { int M = order[1439 - i], h = (M / 60 + 12) % 24; printf(" %02d:%02d(%ds)", h, M % 60, secs[M]); }
    printf("\n");
    // The worst minutes all end in the same digit, which is a fact about the
    // display rather than about the clock: how long a digit reads correctly
    // depends on how many segments it shares with the digit it came from.
    printf("  mean seconds correct by last digit of the minute:\n   ");
    for (int d = 0; d < 10; d++) { long t = 0; int c = 0;
      for (int M = 0; M < 1440; M++) if (M % 10 == d) { t += secs[M]; c++; }
      printf(" %d:%.0fs", d, (double)t / c); }
    printf("\n"); }
  { long total = 0; int worst = 61, worstM = -1;
    for (int M = 0; M < 1440; M++) { int ok = 0;
      for (int s = 0; s < 60; s++) { int64_t g = (int64_t)M * PERIOD + 12800 + (int64_t)s * GPS; if (g / STEP < NSAMP && shown[g / STEP] == M) ok++; }
      total += ok; if (ok < worst) { worst = ok; worstM = M; } }
    printf("current 12800: mean %.1f s, worst minute %d correct for %d s\n", total / 1440.0, worstM, worst); }
  return 0;
}
