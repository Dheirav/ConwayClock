// Engine tests: naive-Life comparison, then the clock: trace + hash to compare
// with the JS engine, timings and memory.
#include "hashlife.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static long rss_mb(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_maxrss / 1024; }
static uint64_t fnv(const uint8_t *b, size_t n) { uint64_t h = 1469598103934665603ULL; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; } return h; }
static char *read_file(const char *path) { FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); } fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); char *s = malloc(n + 1); fread(s, 1, n, f); s[n] = 0; fclose(f); return s; }
int main(int argc, char **argv) {
  hl_init();
  // 1. naive comparison
  const int W = 48, PAD = 40, WW = W + 2 * PAD; srand(7);
  uint8_t *big = calloc(WW * WW, 1), *nb = calloc(WW * WW, 1); Cell *cells = malloc(sizeof(Cell) * W * W); int32_t nc = 0;
  for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) if (rand() % 100 < 35) { big[(y + PAD) * WW + x + PAD] = 1; cells[nc].x = x; cells[nc].y = y; nc++; }
  Universe u; hl_from_cells(&u, cells, nc);
  int steps[5] = { 1, 1, 1, 23, 5 }; int ok = 1; uint8_t *bm = malloc(WW * WW);
  for (int s = 0; s < 5 && ok; s++) {
    hl_advance(&u, steps[s]);
    for (int k = 0; k < steps[s]; k++) { for (int y = 1; y < WW - 1; y++) for (int x = 1; x < WW - 1; x++) { int c = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) c += big[(y + dy) * WW + x + dx]; nb[y * WW + x] = (c == 3 || (c == 2 && big[y * WW + x])) ? 1 : 0; } uint8_t *t = big; big = nb; nb = t; }
    hl_bitmap(&u, -PAD, -PAD, WW, WW, bm);
    if (memcmp(bm, big, WW * WW)) ok = 0;
  }
  printf("C hashlife vs naive: %s\n", ok ? "MATCH" : "FAIL");
  // 2. clock: same trace as tools gctest.js (90 x 64 steps, gc at 20 and 50 when argv[1]=gc)
  char *rle = read_file(argc > 2 ? argv[2] : "../clock.rle"); int32_t n; Cell *cc = hl_parse_rle(rle, 0, &n);
  double t0 = now_ms(); Universe c; hl_from_cells(&c, cc, n); printf("clock: %d cells, level %d, build %.0f ms\n", n, hl_level(&c), now_ms() - t0);
  int gc = argc > 1 && strcmp(argv[1], "gc") == 0; int32_t pops[9]; int np = 0;
  t0 = now_ms();
  for (int i = 0; i < 90; i++) { hl_advance(&c, 64); if (gc && (i == 20 || i == 50)) { HlGcResult g = hl_gc(&c); printf("gc at %d: %d -> %d nodes, memo %u\n", i, g.before, g.after, g.memo); } if (i % 10 == 9) pops[np++] = hl_population(&c); }
  uint8_t *full = malloc((size_t)10100 * 6900); hl_bitmap(&c, c.x0, c.y0, 10100, 6900, full);
  printf("trace: gen %lld pops", (long long)c.generation); for (int i = 0; i < np; i++) printf("%s%d", i ? "," : " ", pops[i]); printf(" fnv %016llx  (%.0f ms)\n", (unsigned long long)fnv(full, (size_t)10100 * 6900), now_ms() - t0);
  // 3. timings
  t0 = now_ms(); hl_advance(&c, 11520 * 2); printf("2-minute warm-up jump: %.1f s, rss %ld MB\n", (now_ms() - t0) / 1e3, rss_mb());
  for (int m = 0; m < 3; m++) { t0 = now_ms(); for (int i = 0; i < 180; i++) hl_advance(&c, 64); HlStats s = hl_stats(); printf("minute of 64-gen steps: %.2f ms/step, nodes %d, memo %u, tables %.0f MB, rss %ld MB\n", (now_ms() - t0) / 180, s.nodes, s.memo, s.bytes / 1e6, rss_mb()); }
  t0 = now_ms(); HlGcResult g = hl_gc(&c); printf("gc: %.0f ms, %d -> %d nodes, memo %u\n", now_ms() - t0, g.before, g.after, g.memo);
  for (int m = 0; m < 2; m++) { t0 = now_ms(); for (int i = 0; i < 180; i++) hl_advance(&c, 64); printf("minute after gc: %.2f ms/step, nodes %d\n", (now_ms() - t0) / 180, hl_stats().nodes); }
  uint8_t *dens = malloc(1254 * 852);
  t0 = now_ms(); for (int i = 0; i < 20; i++) { hl_advance(&c, 64); hl_density_cached(&c, c.x0 - 8, c.y0 - 8, 1254, 852, 3, dens); } printf("advance(64)+cached render: %.2f ms\n", (now_ms() - t0) / 20);
  uint8_t *d2 = malloc(1254 * 852); hl_density(&c, c.x0 - 8, c.y0 - 8, 1254, 852, 3, d2); printf("cached render identical to plain: %s\n", memcmp(dens, d2, 1254 * 852) ? "NO" : "yes");
  // 4. spans: the map built by clearing only the previous frame's ranges must be
  // byte-identical to a full rebuild, and every live pixel must fall inside the
  // span reported for its row. This is what lets the resample skip empty space.
  {
    // The default whole-machine view on 1920x1080, per setup_view() in main.c:
    // 1550x925 map at 1/8 from (-300, 1677). It is wider and taller than the
    // pattern, so it also exercises rows the descent never reaches.
    const int SW = 1550, SH = 925; const int64_t sx = -300, sy = 1677;
    uint8_t *ref = malloc((size_t)SW * SH), *spa = calloc((size_t)SW * SH, 1);
    HlSpan *cur = malloc(sizeof(HlSpan) * SH), *prev = malloc(sizeof(HlSpan) * SH);
    for (int i = 0; i < SH; i++) { prev[i].lo = SW; prev[i].hi = 0; }
    int bad = 0, outside = 0; long covered = 0; double tFull = 0, tSpan = 0;
    for (int f = 0; f < 200; f++) {
      hl_advance(&c, 32);
      double a = now_ms(); hl_density_cached(&c, sx, sy, SW, SH, 3, ref); tFull += now_ms() - a;
      for (int y = 0; y < SH; y++) if (prev[y].lo < prev[y].hi) memset(spa + (size_t)y * SW + prev[y].lo, 0, prev[y].hi - prev[y].lo);
      a = now_ms(); hl_density_spans(&c, sx, sy, SW, SH, 3, spa, cur); tSpan += now_ms() - a;
      if (memcmp(ref, spa, (size_t)SW * SH)) bad++;
      for (int y = 0; y < SH; y++) { const uint8_t *r = ref + (size_t)y * SW;
        for (int x = 0; x < SW; x++) if (r[x] && (x < cur[y].lo || x >= cur[y].hi)) { outside++; break; }
        if (cur[y].lo < cur[y].hi) covered += cur[y].hi - cur[y].lo; }
      HlSpan *t = cur; cur = prev; prev = t;
    }
    printf("spans: %d/200 frames differ from a full rebuild, %d rows with live pixels outside their span\n", bad, outside);
    printf("spans cover %.1f%% of the %dx%d view map (what the resample can skip); density fill %.2f ms full vs %.2f ms with spans\n",
           100.0 * covered / (200.0 * SW * SH), SW, SH, tFull / 200, tSpan / 200);
    if (bad || outside) ok = 0;
    free(ref); free(spa); free(cur); free(prev);
  }
  // 5. The span -> output-column mapping the restricted resample relies on.
  // An output column x blends map columns mapX[x] and mapX[x]+1, so a span
  // [lo, hi) of changed map columns invalidates exactly those x with
  // mapX[x] in [lo-1, hi). render() takes that range as [invX[lo-1], invX[hi]).
  // Too wide only wastes work; too narrow leaves stale pixels on screen.
  {
    const int pw = 1550, ph = 925;               // default view, per setup_view()
    double scale = (1920.0 / pw < 1080.0 / ph) ? 1920.0 / pw : 1080.0 / ph;
    int dw = (int)(pw * scale);
    int *mapX = malloc(sizeof(int) * dw), *invX = malloc(sizeof(int) * (pw + 2));
    for (int i = 0; i < dw; i++) { double sv = (i + 0.5) / scale - 0.5; if (sv < 0) sv = 0; int k = (int)sv; if (k > pw - 2) k = pw - 2; mapX[i] = k; }
    for (int k = 0; k <= pw + 1; k++) invX[k] = dw;
    for (int x = dw - 1; x >= 0; x--) invX[mapX[x]] = x;
    for (int k = pw; k >= 0; k--) if (invX[k] > invX[k + 1]) invX[k] = invX[k + 1];
    int narrow = 0, checked = 0;
    for (int t = 0; t < 20000; t++) {
      int lo = rand() % pw, hi = lo + 1 + rand() % (pw - lo);
      int e0 = dw, e1 = 0;                        // columns that actually read [lo, hi)
      for (int x = 0; x < dw; x++) { int k = mapX[x]; if ((k >= lo && k < hi) || (k + 1 >= lo && k + 1 < hi)) { if (x < e0) e0 = x; if (x + 1 > e1) e1 = x + 1; } }
      int g0 = invX[lo > 0 ? lo - 1 : 0], g1 = invX[hi < pw ? hi : pw];
      if (e0 < e1 && (g0 > e0 || g1 < e1)) narrow++;
      checked++;
    }
    printf("span->column mapping: %d/%d random spans where the resample range was too narrow\n", narrow, checked);
    if (narrow) ok = 0;
    free(mapX); free(invX);
  }
  t0 = now_ms(); hl_advance(&c, 11520LL * 720); printf("12-hour jump: %.1f s, gen %lld, rss %ld MB\n", (now_ms() - t0) / 1e3, (long long)c.generation, rss_mb());
  return ok ? 0 : 1;
}
