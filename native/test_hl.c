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
  t0 = now_ms(); hl_advance(&c, 11520LL * 720); printf("12-hour jump: %.1f s, gen %lld, rss %ld MB\n", (now_ms() - t0) / 1e3, (long long)c.generation, rss_mb());
  return ok ? 0 : 1;
}
