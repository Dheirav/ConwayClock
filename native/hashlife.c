// Hashlife in C: a port of ../hashlife.js. Memoised quadtree Life (Gosper).
// Nodes live in flat arrays indexed by int32; node 0 is the dead cell, node 1
// the live cell. (node, j) -> "centre advanced 2^j generations" is cached in
// an open-addressing table. Single global engine instance.
#include "hashlife.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// One node = 28 bytes, contiguous: children, population, and one cached
// result (the centre advanced 2^resJ generations). A single slot suffices
// because results for j >= level-2 do not depend on j, and nodes high enough
// for j to matter are fresh every step anyway.
typedef struct { int32_t a, b, c, d, pop, res; uint8_t level, resJ; uint8_t pad[2]; } Node;
static Node *N;
static int32_t count_, cap_;
static int32_t *HT; static uint32_t htMask;
static int32_t emptyCache[64]; static int emptyLen;
// tile cache: node -> tile index; tiles are 8x8 bytes
static int32_t *TK, *TV; static uint32_t tMask, tCount;
static uint8_t *tiles; static uint32_t tileCount, tileCap;
#define A(n) (N[n].a)
#define B(n) (N[n].b)
#define C(n) (N[n].c)
#define D(n) (N[n].d)
#define POP(n) (N[n].pop)
#define LEVEL(n) (N[n].level)

static uint32_t hash4(int32_t a, int32_t b, int32_t c, int32_t d) {
  return ((uint32_t)a * 0x9E3779B1u) ^ ((uint32_t)b * 0x85EBCA77u) ^ ((uint32_t)c * 0xC2B2AE3Du) ^ ((uint32_t)d * 0x27D4EB2Fu);
}
static void rehash_nodes(void) {
  free(HT); HT = malloc(sizeof(int32_t) * (size_t)cap_ * 2); memset(HT, 0xff, sizeof(int32_t) * (size_t)cap_ * 2); htMask = (uint32_t)cap_ * 2 - 1;
  for (int32_t n = 2; n < count_; n++) { uint32_t i = hash4(A(n), B(n), C(n), D(n)) & htMask; while (HT[i] != -1) i = (i + 1) & htMask; HT[i] = n; }
}
static void grow_nodes(void) {
  cap_ *= 2;
  N = realloc(N, sizeof(Node) * (size_t)cap_);
  rehash_nodes();
}
static void tiles_clear(void) {
  if (TK) memset(TK, 0xff, sizeof(int32_t) * (tMask + 1));
  tCount = 0; tileCount = 0;
}
void hl_init(void) {
  cap_ = 1 << 18; count_ = 2;
  N = calloc(cap_, sizeof(Node));
  N[0].res = N[1].res = -1; N[1].pop = 1;
  rehash_nodes();
  emptyCache[0] = 0; emptyLen = 1;
  // Sized from measurement rather than guesswork: the whole machine at 1/8
  // settles around 45,000 tiles between collections, so 131,072 slots keeps
  // about three times the headroom the flush threshold wants, for a quarter of
  // the memory the previous 524,288 took.
  tMask = (1 << 18) - 1; TK = malloc(sizeof(int32_t) * (tMask + 1)); TV = malloc(sizeof(int32_t) * (tMask + 1)); tileCap = 1 << 17; tiles = malloc((size_t)tileCap * 64); tiles_clear();
}
static int32_t join(int32_t a, int32_t b, int32_t c, int32_t d) {
  uint32_t i = hash4(a, b, c, d) & htMask;
  for (;;) { int32_t n = HT[i]; if (n == -1) break; if (A(n) == a && B(n) == b && C(n) == c && D(n) == d) return n; i = (i + 1) & htMask; }
  if (count_ >= cap_ - 1) { grow_nodes(); return join(a, b, c, d); }
  int32_t n = count_++;
  N[n].a = a; N[n].b = b; N[n].c = c; N[n].d = d; N[n].pop = POP(a) + POP(b) + POP(c) + POP(d); N[n].level = LEVEL(a) + 1; N[n].res = -1; N[n].resJ = 0;
  HT[i] = n; return n;
}
static int32_t empty(int level) {
  while (emptyLen <= level) { int32_t e = emptyCache[emptyLen - 1]; emptyCache[emptyLen++] = join(e, e, e, e); }
  return emptyCache[level];
}
static int32_t expand(int32_t n) { int32_t e = empty(LEVEL(n) - 1); return join(join(e, e, e, A(n)), join(e, e, B(n), e), join(e, C(n), e, e), join(D(n), e, e, e)); }

static int32_t life4x4(int32_t n) {
  uint8_t bits[16]; int32_t sub[4] = { A(n), B(n), C(n), D(n) };
  for (int q = 0; q < 4; q++) { int32_t s = sub[q]; int ox = (q & 1) * 2, oy = (q >> 1) * 2;
    bits[oy * 4 + ox] = (uint8_t)POP(A(s)); bits[oy * 4 + ox + 1] = (uint8_t)POP(B(s)); bits[(oy + 1) * 4 + ox] = (uint8_t)POP(C(s)); bits[(oy + 1) * 4 + ox + 1] = (uint8_t)POP(D(s)); }
  int32_t r[4]; int idx = 0;
  for (int y = 1; y <= 2; y++) for (int x = 1; x <= 2; x++) {
    int k = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) k += bits[(y + dy) * 4 + x + dx];
    r[idx++] = (k == 3 || (k == 2 && bits[y * 4 + x])) ? 1 : 0;
  }
  return join(r[0], r[1], r[2], r[3]);
}
static inline int32_t centre(int32_t n) { return join(D(A(n)), C(B(n)), B(C(n)), A(D(n))); }
static inline int32_t horiz(int32_t w, int32_t e) { return join(B(w), A(e), D(w), C(e)); }
static inline int32_t vert(int32_t n, int32_t s) { return join(C(n), D(n), A(s), B(s)); }

static int32_t successor(int32_t n, int j) {
  if (POP(n) == 0) return empty(LEVEL(n) - 1);
  if (LEVEL(n) == 2) return life4x4(n);
  // For j >= level-2 the result is the full step and independent of j.
  int jj = j < LEVEL(n) - 2 ? j : LEVEL(n) - 2;
  if (N[n].res != -1 && N[n].resJ == jj) return N[n].res;
  int32_t a = A(n), b = B(n), c = C(n), d = D(n);
  int32_t c1 = successor(a, j), c2 = successor(horiz(a, b), j), c3 = successor(b, j);
  int32_t c4 = successor(vert(a, c), j), c5 = successor(centre(n), j), c6 = successor(vert(b, d), j);
  int32_t c7 = successor(c, j), c8 = successor(horiz(c, d), j), c9 = successor(d, j);
  int32_t r;
  if (j < LEVEL(n) - 2) r = join(centre(join(c1, c2, c4, c5)), centre(join(c2, c3, c5, c6)), centre(join(c4, c5, c7, c8)), centre(join(c5, c6, c8, c9)));
  else r = join(successor(join(c1, c2, c4, c5), j), successor(join(c2, c3, c5, c6), j), successor(join(c4, c5, c7, c8), j), successor(join(c5, c6, c8, c9), j));
  N[n].res = r; N[n].resJ = (uint8_t)jj;
  return r;
}

// ---- Universe -------------------------------------------------------------
int hl_level(const Universe *u) { return LEVEL(u->root); }
int64_t hl_size(const Universe *u) { return (int64_t)1 << LEVEL(u->root); }
int32_t hl_population(const Universe *u) { return POP(u->root); }

static int32_t build(Cell *list, int32_t n, int lvl, int64_t ox, int64_t oy) {
  if (n == 0) return empty(lvl);
  if (lvl == 0) return 1;
  int64_t half = (int64_t)1 << (lvl - 1);
  // 4-way partition into a scratch copy, then recurse on the ranges
  Cell *tmp = malloc(sizeof(Cell) * n); int32_t cnt[4] = {0,0,0,0}, off[4];
  for (int32_t i = 0; i < n; i++) cnt[((list[i].y - oy) >= half ? 2 : 0) + ((list[i].x - ox) >= half ? 1 : 0)]++;
  off[0] = 0; for (int q = 1; q < 4; q++) off[q] = off[q - 1] + cnt[q - 1];
  int32_t pos[4] = { off[0], off[1], off[2], off[3] };
  for (int32_t i = 0; i < n; i++) { int q = ((list[i].y - oy) >= half ? 2 : 0) + ((list[i].x - ox) >= half ? 1 : 0); tmp[pos[q]++] = list[i]; }
  int32_t r = join(build(tmp + off[0], cnt[0], lvl - 1, ox, oy), build(tmp + off[1], cnt[1], lvl - 1, ox + half, oy),
                   build(tmp + off[2], cnt[2], lvl - 1, ox, oy + half), build(tmp + off[3], cnt[3], lvl - 1, ox + half, oy + half));
  free(tmp); return r;
}
void hl_from_cells(Universe *u, Cell *cells, int32_t n) {
  u->generation = 0;
  if (n == 0) { u->root = empty(3); u->x0 = -4; u->y0 = -4; return; }
  int64_t minx = cells[0].x, miny = cells[0].y, maxx = minx, maxy = miny;
  for (int32_t i = 1; i < n; i++) { if (cells[i].x < minx) minx = cells[i].x; if (cells[i].y < miny) miny = cells[i].y; if (cells[i].x > maxx) maxx = cells[i].x; if (cells[i].y > maxy) maxy = cells[i].y; }
  int level = 1; int64_t span = (maxx - minx + 1 > maxy - miny + 1) ? maxx - minx + 1 : maxy - miny + 1;
  while (((int64_t)1 << level) < span) level++;
  u->root = build(cells, n, level, minx, miny); u->x0 = minx; u->y0 = miny;
}
static void u_expand(Universe *u) { int64_t q = hl_size(u) >> 1; u->root = expand(u->root); u->x0 -= q; u->y0 -= q; }
static void u_trim(Universe *u) {
  while (LEVEL(u->root) > 3) { int32_t r = u->root; int64_t q = hl_size(u) >> 2;
    if (POP(D(A(r))) + POP(C(B(r))) + POP(B(C(r))) + POP(A(D(r))) != POP(r)) break;
    u->root = centre(r); u->x0 += q; u->y0 += q; }
}
void hl_advance(Universe *u, int64_t gens) {
  for (int j = 0; ((int64_t)1 << j) <= gens; j++) if (gens & ((int64_t)1 << j)) {
    while (LEVEL(u->root) < j + 3 || POP(centre(centre(u->root))) != POP(u->root)) u_expand(u);
    int64_t q = hl_size(u) >> 2;
    u->root = successor(u->root, j);
    u->x0 += q; u->y0 += q; u->generation += (int64_t)1 << j;
  }
  u_trim(u);
}

typedef struct { uint8_t *out; int64_t x, y; int w, h; int z; HlSpan *rows; } Rect;
static void bitmap_rec(const Rect *r, int32_t n, int64_t nx, int64_t ny) {
  if (POP(n) == 0) return;
  int64_t s = (int64_t)1 << LEVEL(n);
  if (nx + s <= r->x || ny + s <= r->y || nx >= r->x + r->w || ny >= r->y + r->h) return;
  if (LEVEL(n) == 0) { r->out[(ny - r->y) * r->w + (nx - r->x)] = 1; return; }
  int64_t half = s >> 1;
  bitmap_rec(r, A(n), nx, ny); bitmap_rec(r, B(n), nx + half, ny); bitmap_rec(r, C(n), nx, ny + half); bitmap_rec(r, D(n), nx + half, ny + half);
}
void hl_bitmap(const Universe *u, int64_t x, int64_t y, int w, int h, uint8_t *out) {
  memset(out, 0, (size_t)w * h); Rect r = { out, x, y, w, h, 0 }; bitmap_rec(&r, u->root, u->x0, u->y0);
}
static void density_rec(const Rect *r, int32_t n, int64_t nx, int64_t ny) {
  if (POP(n) == 0) return;
  int64_t s = (int64_t)1 << LEVEL(n), blk = (int64_t)1 << r->z;
  if (nx + s <= r->x || ny + s <= r->y || nx >= r->x + (int64_t)r->w * blk || ny >= r->y + (int64_t)r->h * blk) return;
  if (LEVEL(n) <= r->z) { int64_t i = ((ny - r->y) >> r->z) * r->w + ((nx - r->x) >> r->z); int v = r->out[i] + POP(n); r->out[i] = v > 255 ? 255 : (uint8_t)v; return; }
  int64_t half = s >> 1;
  density_rec(r, A(n), nx, ny); density_rec(r, B(n), nx + half, ny); density_rec(r, C(n), nx, ny + half); density_rec(r, D(n), nx + half, ny + half);
}
void hl_density(const Universe *u, int64_t x, int64_t y, int w, int h, int z, uint8_t *out) {
  memset(out, 0, (size_t)w * h); Rect r = { out, x, y, w, h, z }; density_rec(&r, u->root, u->x0, u->y0);
}
// Tile cache: tiles are for nodes at level z+3 (8x8 output pixels).
static uint8_t *tile_for(int32_t n, int z) {
  uint32_t i = ((uint32_t)n * 0x9E3779B1u) & tMask;
  for (;;) { int32_t k = TK[i]; if (k == n) return tiles + (size_t)TV[i] * 64; if (k == -1) break; i = (i + 1) & tMask; }
  if (tCount * 10 > (tMask + 1) * 7 || tileCount >= tileCap) { // too many tiles: flush the cache
    tiles_clear(); i = ((uint32_t)n * 0x9E3779B1u) & tMask;
  }
  uint8_t *t = tiles + (size_t)tileCount * 64; memset(t, 0, 64);
  Rect r = { t, 0, 0, 8, 8, z }; density_rec(&r, n, 0, 0);
  TK[i] = n; TV[i] = (int32_t)tileCount++; tCount++;
  return t;
}
static void cached_rec(const Rect *r, int32_t n, int64_t nx, int64_t ny) {
  if (POP(n) == 0) return;
  int64_t s = (int64_t)1 << LEVEL(n), blk = (int64_t)1 << r->z;
  if (nx + s <= r->x || ny + s <= r->y || nx >= r->x + (int64_t)r->w * blk || ny >= r->y + (int64_t)r->h * blk) return;
  if (LEVEL(n) == r->z + 3) {
    // Locals: writes through the uint8_t* may alias the Rect, which would
    // otherwise force reloads of w/h in the loop.
    const uint8_t *t = tile_for(n, r->z); const int w = r->w, h = r->h; uint8_t *out = r->out;
    int64_t px = (nx - r->x) >> r->z, py = (ny - r->y) >> r->z;
    if (r->rows) { // the tile's columns, clipped, recorded against each of its 8 rows
      int32_t lo = px < 0 ? 0 : (int32_t)px, hi = px + 8 > w ? w : (int32_t)(px + 8);
      if (lo < hi) for (int ty = 0; ty < 8; ty++) { int64_t oy = py + ty; if (oy < 0 || oy >= h) continue;
        HlSpan *sp = &r->rows[oy]; if (lo < sp->lo) sp->lo = lo; if (hi > sp->hi) sp->hi = hi; } }
    if (px >= 0 && py >= 0 && px + 8 <= w && py + 8 <= h) { for (int ty = 0; ty < 8; ty++) memcpy(out + (py + ty) * w + px, t + ty * 8, 8); return; }
    for (int ty = 0; ty < 8; ty++) { int64_t oy = py + ty; if (oy < 0 || oy >= h) continue;
      for (int tx = 0; tx < 8; tx++) { int64_t ox = px + tx; if (ox >= 0 && ox < w) out[oy * w + ox] = t[ty * 8 + tx]; } }
    return;
  }
  int64_t half = s >> 1;
  cached_rec(r, A(n), nx, ny); cached_rec(r, B(n), nx + half, ny); cached_rec(r, C(n), nx, ny + half); cached_rec(r, D(n), nx + half, ny + half);
}
void hl_density_cached(const Universe *u, int64_t x, int64_t y, int w, int h, int z, uint8_t *out) {
  memset(out, 0, (size_t)w * h); Rect r = { out, x, y, w, h, z }; cached_rec(&r, u->root, u->x0, u->y0);
}
void hl_density_spans(const Universe *u, int64_t x, int64_t y, int w, int h, int z, uint8_t *out, HlSpan *rows) {
  for (int i = 0; i < h; i++) { rows[i].lo = w; rows[i].hi = 0; }
  Rect r = { out, x, y, w, h, z, rows }; cached_rec(&r, u->root, u->x0, u->y0);
}

// Compaction: keep nodes reachable from the root plus memo entries whose key
// and value both survive (and their subtrees), renumber, rebuild tables.
HlGcResult hl_gc(Universe *u) {
  HlGcResult res; res.before = count_;
  uint8_t *mark = calloc(count_, 1); mark[0] = mark[1] = 1;
  int32_t *stack = malloc(sizeof(int32_t) * (size_t)count_); int32_t sp = 0;
  #define PUSH(v) do { if (!mark[v]) stack[sp++] = (v); } while (0)
  #define DRAIN() while (sp) { int32_t m = stack[--sp]; if (mark[m]) continue; mark[m] = 1; if (N[m].level > 0) { PUSH(N[m].a); PUSH(N[m].b); PUSH(N[m].c); PUSH(N[m].d); } }
  PUSH(u->root); DRAIN();
  // Cached results of surviving nodes are worth keeping: mark their targets too.
  int added = 1;
  while (added) { added = 0; for (int32_t n = 2; n < count_; n++) if (mark[n] && N[n].res != -1 && !mark[N[n].res]) { PUSH(N[n].res); DRAIN(); added = 1; } }
  int32_t *map = malloc(sizeof(int32_t) * (size_t)count_); int32_t k = 0;
  for (int32_t n = 0; n < count_; n++) map[n] = mark[n] ? k++ : -1;
  // Collection usually frees most of the table, and the array has only ever
  // grown, so hand the memory back: keep room to double before the next growth,
  // and never enlarge here.
  int32_t newCap = 1 << 18;
  while (newCap < k * 2 && newCap < cap_) newCap <<= 1;
  if (newCap > cap_) newCap = cap_;
  Node *NN = calloc(newCap, sizeof(Node));
  for (int32_t n = 0; n < count_; n++) if (mark[n]) { Node *m = &NN[map[n]]; *m = N[n];
    if (N[n].level) { m->a = map[N[n].a]; m->b = map[N[n].b]; m->c = map[N[n].c]; m->d = map[N[n].d]; }
    m->res = (N[n].res != -1 && mark[N[n].res]) ? map[N[n].res] : -1; }
  free(N); N = NN; count_ = k; cap_ = newCap;
  rehash_nodes();
  emptyLen = 1; tiles_clear();
  u->root = map[u->root];
  uint32_t kept = 0; for (int32_t n = 0; n < count_; n++) if (N[n].res != -1) kept++;
  free(mark); free(stack); free(map);
  res.after = count_; res.memo = kept;
  return res;
}
HlStats hl_stats(void) {
  HlStats s; s.nodes = count_; s.cap = cap_; s.tiles = tileCount;
  uint32_t kept = 0; for (int32_t n = 0; n < count_; n++) if (N[n].res != -1) kept++; s.memo = kept;
  s.bytes = (size_t)cap_ * sizeof(Node) + (size_t)(htMask + 1) * 4 + (size_t)(tMask + 1) * 8 + (size_t)tileCap * 64;
  return s;
}

// ---- RLE -------------------------------------------------------------------
// Returns malloc'd cells; if use_pos and a "#CXRLE Pos=x,y" header exists, the
// cells are offset by it. *count receives the number of cells.
Cell *hl_parse_rle(const char *text, int use_pos, int32_t *count) {
  int64_t px = 0, py = 0;
  const char *p = text;
  int32_t cap = 1 << 16, n = 0; Cell *cells = malloc(sizeof(Cell) * cap);
  int64_t x = 0, y = 0;
  while (*p) {
    if (*p == '#') { if (use_pos && strncmp(p, "#CXRLE", 6) == 0) { const char *q = strstr(p, "Pos="); if (q) { px = strtoll(q + 4, (char **)&q, 10); if (*q == ',') py = strtoll(q + 1, NULL, 10); } }
      while (*p && *p != '\n') p++;
      if (*p) p++; continue; }
    if (*p == 'x' && (p[1] == ' ' || p[1] == '=')) { while (*p && *p != '\n') p++; if (*p) p++; continue; }
    if (*p == '\n' || *p == '\r' || *p == ' ') { p++; continue; }
    int64_t run = 0; while (*p >= '0' && *p <= '9') run = run * 10 + (*p++ - '0'); if (run == 0) run = 1;
    char t = *p++;
    if (t == 'b') x += run;
    else if (t == 'o') { for (int64_t i = 0; i < run; i++) { if (n == cap) { cap *= 2; cells = realloc(cells, sizeof(Cell) * cap); } cells[n].x = x++ + px; cells[n].y = y + py; n++; } }
    else if (t == '$') { y += run; x = 0; }
    else if (t == '!') break;
    else break;
  }
  *count = n; return cells;
}
