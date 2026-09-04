#ifndef HASHLIFE_H
#define HASHLIFE_H
#include <stdint.h>
#include <stddef.h>
typedef struct { int64_t x, y; } Cell;
typedef struct { int32_t root; int64_t x0, y0; int64_t generation; } Universe;
typedef struct { int32_t before, after; uint32_t memo; } HlGcResult;
typedef struct { int32_t nodes, cap; uint32_t memo, tiles; size_t bytes; } HlStats;
void hl_init(void);
void hl_from_cells(Universe *u, Cell *cells, int32_t n);
void hl_advance(Universe *u, int64_t gens);
int hl_level(const Universe *u);
int64_t hl_size(const Universe *u);
int32_t hl_population(const Universe *u);
void hl_bitmap(const Universe *u, int64_t x, int64_t y, int w, int h, uint8_t *out);
void hl_density(const Universe *u, int64_t x, int64_t y, int w, int h, int z, uint8_t *out);
void hl_density_cached(const Universe *u, int64_t x, int64_t y, int w, int h, int z, uint8_t *out);
HlGcResult hl_gc(Universe *u);
HlStats hl_stats(void);
Cell *hl_parse_rle(const char *text, int use_pos, int32_t *count);
#endif
