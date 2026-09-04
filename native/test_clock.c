// Does the wallpaper show the right time? Loads the embedded snapshots, runs
// the machine to the generation the program would run it to for a given
// minute of the day, reads the seven-segment display back off the grid, and
// compares it with the clock. This is the end-to-end check: it exercises the
// snapshots, the gzip decoder, the engine, the sync arithmetic and the
// machine's own display, and it is what catches a mistake like reading the
// cycle as 12 hours when it is 24.
#include "hashlife.h"
#include "inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const int SNAP_COUNT; extern const int SNAP_MINUTE[]; extern const size_t SNAP_OFF[]; extern const unsigned char SNAP_DATA[];
#define PERIOD 11520
#define DISPLAY_LAG 12800   // must match main.c

static Universe u;
static int count(int x, int y, int w, int h) {
  uint8_t *b = malloc((size_t)w * h); hl_bitmap(&u, x, y, w, h, b);
  int n = 0; for (int i = 0; i < w * h; i++) n += b[i]; free(b); return n;
}
// Segment sample rectangles, in pattern coordinates. A lit segment is a bundle
// of glider streams; the corner hardware is present either way, so every
// rectangle stays clear of the corners.
#define TH 30
static char digit(int x0, int x1) {
  int xm = (x0 + x1) / 2, s[7];
  s[0] = count(xm - 300, 4130, 600, 200);   // a, top
  s[6] = count(xm - 300, 5330, 600, 200);   // g, middle
  s[3] = count(xm - 300, 6530, 600, 200);   // d, bottom
  s[5] = count(x0, 4500, xm - x0, 650);     // f, upper left
  s[1] = count(xm, 4500, x1 - xm, 650);     // b, upper right
  s[4] = count(x0, 5700, xm - x0, 600);     // e, lower left
  s[2] = count(xm, 5700, x1 - xm, 600);     // c, lower right
  char on[8]; int k = 0; for (int i = 0; i < 7; i++) if (s[i] > TH) on[k++] = (char)('a' + i); on[k] = 0;
  static const char *pat[10] = { "abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg" };
  for (int d = 0; d < 10; d++) if (!strcmp(on, pat[d])) return (char)('0' + d);
  return '?';
}
static void read_display(char *out) {
  out[0] = count(2040, 4500, 360, 1800) > TH ? '1' : ' ';   // the hour's leading 1
  out[1] = digit(3080, 4720);
  out[2] = ':';
  out[3] = digit(5880, 7320);
  out[4] = digit(8200, 9720);
  out[5] = 0;
}
// The AM/PM box: an outline (about 4,890 cells) in the morning, filled (about
// 9,956) in the afternoon.
static int pm_lit(void) { return count(100, 4150, 580, 600) > 7400; }

static void load_minute(int m) {
  int best = 0; for (int i = 0; i < SNAP_COUNT; i++) if (SNAP_MINUTE[i] <= m) best = i;
  size_t len; uint8_t *text = gunzip(SNAP_DATA + SNAP_OFF[best], SNAP_OFF[best + 1] - SNAP_OFF[best], &len);
  if (!text) { fprintf(stderr, "snapshot %d failed to decompress\n", best); exit(2); }
  int32_t n; Cell *cells = hl_parse_rle((const char *)text, 1, &n); free(text);
  hl_from_cells(&u, cells, n); free(cells);
  u.generation = (int64_t)SNAP_MINUTE[best] * PERIOD;
}

int main(int argc, char **argv) {
  hl_init();
  // Minutes of the machine's 24-hour cycle (0 = 12:00 PM): a plain minute, a
  // tens rollover, hour rollovers, noon, midnight, the AM/PM flip, and the
  // last minute of the cycle.
  static const int CASES[] = { 5, 10, 59, 60, 130, 359, 360, 719, 720, 721, 1079, 1439 };
  int n = (int)(sizeof CASES / sizeof CASES[0]), fails = 0;
  if (argc > 1 && atoi(argv[1]) > 0 && atoi(argv[1]) < n) n = atoi(argv[1]);
  clock_t t0 = clock();
  puts("Each minute is sampled every 4 seconds. The machine spends part of every");
  puts("minute redrawing, so a sample can be mid-change; what must hold is that the");
  puts("right time is shown for a good part of the minute, with the right AM/PM.\n");
  for (int i = 0; i < n; i++) {
    int m = CASES[i];
    load_minute(m);
    hl_advance(&u, (int64_t)m * PERIOD + DISPLAY_LAG - u.generation);
    int hour24 = (m / 60 + 12) % 24, h12 = hour24 % 12; if (!h12) h12 = 12;
    char want[8]; sprintf(want, "%2d:%02d", h12, m % 60);
    int wantPm = m < 720;
    int correct = 0, pmBad = 0, first = -1, last = -1;
    for (int s = 0; s < 60; s += 4) {
      if (s) hl_advance(&u, 4 * 192);
      char got[8]; read_display(got);
      if (!strcmp(got, want)) { correct++; if (first < 0) first = s; last = s;
        if (pm_lit() != wantPm) pmBad++; }
    }
    // At least three samples (twelve seconds of the minute) must read correctly,
    // and whenever the time is right the AM/PM must be too.
    int ok = correct >= 3 && pmBad == 0;
    printf("%-4s %02d:%02d  (cycle minute %4d)  correct at seconds %2d-%2d of 60, %2d of 15 samples%s\n",
           ok ? "ok" : "FAIL", hour24, m % 60, m, first, last, correct, pmBad ? "  AM/PM WRONG" : "");
    if (!ok) fails++;
    hl_gc(&u);
  }
  printf("\n%d of %d times failed (%.1f s)\n", fails, n, (double)(clock() - t0) / CLOCKS_PER_SEC);
  return fails ? 1 : 0;
}
