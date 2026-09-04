// The pattern's colon is two discs of 2x2 still-life blocks (about 22,000 cells
// each) that render as flat dots. This swaps them for discs of pulsars, the
// period-3 oscillator, so the colon breathes under Life's own rules. The discs
// sit 200 cells clear of anything else and nothing ever enters their region
// (the blocks survive the whole 24-hour cycle untouched), so the rest of the
// machine, and the precomputed snapshots, are unaffected.
#include "colon.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Disc footprints in pattern coordinates (measured): x 4990..5500, y 4680..5140 and y 5780..6180.
static const struct { int x0, x1, y0, y1, cx, cy; } DISC[2] = { { 4990, 5500, 4680, 5140, 5250, 4905 }, { 4990, 5500, 5780, 6180, 5250, 5975 } };
static const char *PULSAR[13] = { "..OOO...OOO..", ".............", "O....O.O....O", "O....O.O....O", "O....O.O....O", "..OOO...OOO..", ".............", "..OOO...OOO..", "O....O.O....O", "O....O.O....O", "O....O.O....O", ".............", "..OOO...OOO.." };

// mode: 0 = leave the machine's blocks, 1 = pulsars, 2 = remove (hide).
// Returns a new malloc'd cell array; *n is updated.
Cell *colon_apply(Cell *cells, int32_t *n, int mode) {
  if (mode == 0) return cells;
  Cell *out = malloc(sizeof(Cell) * ((size_t)*n + 40000)); int32_t k = 0;
  for (int32_t i = 0; i < *n; i++) { int inside = 0;
    for (int d = 0; d < 2; d++) if (cells[i].x >= DISC[d].x0 && cells[i].x <= DISC[d].x1 && cells[i].y >= DISC[d].y0 && cells[i].y <= DISC[d].y1) inside = 1;
    if (!inside) out[k++] = cells[i]; }
  if (mode == 1) {
    const int pitch = 26, r = 150;   // 15x15 envelope per pulsar plus an 11-cell gap: 7-11 % density, so brightness pulses
    for (int d = 0; d < 2; d++) for (int gy = -6; gy <= 6; gy++) for (int gx = -6; gx <= 6; gx++) {
      double px = DISC[d].cx + gx * pitch, py = DISC[d].cy + gy * pitch; if (hypot(px - DISC[d].cx, py - DISC[d].cy) > r) continue;
      for (int y = 0; y < 13; y++) for (int x = 0; x < 13; x++) if (PULSAR[y][x] == 'O') { out[k].x = (int64_t)px - 6 + x; out[k].y = (int64_t)py - 6 + y; k++; } }
  }
  free(cells); *n = k; return out;
}
