// Minimal gzip/DEFLATE decoder (RFC 1951/1952) for loading the snapshots.
// Returns a malloc'd buffer and its length, or NULL on error.
#include "inflate.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *p, *end; uint32_t bitbuf; int bitcnt; uint8_t *out; size_t outLen, outCap; int err; } St;
static int bit(St *s) { if (s->bitcnt == 0) { if (s->p >= s->end) { s->err = 1; return 0; } s->bitbuf = *s->p++; s->bitcnt = 8; } int b = s->bitbuf & 1; s->bitbuf >>= 1; s->bitcnt--; return b; }
static uint32_t bits(St *s, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v |= (uint32_t)bit(s) << i; return v; }
static void put(St *s, uint8_t c) { if (s->outLen == s->outCap) { s->outCap = s->outCap ? s->outCap * 2 : 1 << 20; s->out = realloc(s->out, s->outCap); } s->out[s->outLen++] = c; }

typedef struct { uint16_t count[16]; uint16_t symbol[320]; } Huff;
static int build(Huff *h, const uint8_t *lengths, int n) {
  memset(h->count, 0, sizeof h->count); for (int i = 0; i < n; i++) h->count[lengths[i]]++; h->count[0] = 0;
  uint16_t offs[16]; offs[1] = 0; for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
  for (int i = 0; i < n; i++) if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
  return 0;
}
static int decode(St *s, const Huff *h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) { code |= bit(s); int count = h->count[len]; if (code - count < first) return h->symbol[index + (code - first)]; index += count; first += count; first <<= 1; code <<= 1; if (s->err) return -1; }
  return -1;
}
static const uint16_t LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t LEXT[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t DEXT[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static void codes(St *s, const Huff *lc, const Huff *dc) {
  for (;;) {
    int sym = decode(s, lc); if (sym < 0 || s->err) { s->err = 1; return; }
    if (sym < 256) put(s, (uint8_t)sym);
    else if (sym == 256) return;
    else { sym -= 257; if (sym >= 29) { s->err = 1; return; } int len = LBASE[sym] + (int)bits(s, LEXT[sym]);
      int ds = decode(s, dc); if (ds < 0 || ds >= 30) { s->err = 1; return; } size_t dist = DBASE[ds] + bits(s, DEXT[ds]);
      if (dist > s->outLen) { s->err = 1; return; }
      for (int i = 0; i < len; i++) put(s, s->out[s->outLen - dist]); }
  }
}
static void fixed(St *s) {
  uint8_t l[288]; int i = 0; for (; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
  Huff lc, dc; build(&lc, l, 288); uint8_t d[30]; for (i = 0; i < 30; i++) d[i] = 5; build(&dc, d, 30); codes(s, &lc, &dc);
}
static void dynamic(St *s) {
  static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
  int nlen = (int)bits(s, 5) + 257, ndist = (int)bits(s, 5) + 1, ncode = (int)bits(s, 4) + 4;
  if (nlen > 286 || ndist > 30) { s->err = 1; return; }
  uint8_t lengths[320]; memset(lengths, 0, sizeof lengths);
  for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)bits(s, 3);
  Huff cl; build(&cl, lengths, 19);
  int idx = 0;
  while (idx < nlen + ndist) {
    int sym = decode(s, &cl); if (sym < 0 || s->err) { s->err = 1; return; }
    if (sym < 16) lengths[idx++] = (uint8_t)sym;
    else { int rep = 0; uint8_t val = 0;
      if (sym == 16) { if (idx == 0) { s->err = 1; return; } val = lengths[idx - 1]; rep = 3 + (int)bits(s, 2); }
      else if (sym == 17) rep = 3 + (int)bits(s, 3); else rep = 11 + (int)bits(s, 7);
      if (idx + rep > nlen + ndist) { s->err = 1; return; }
      while (rep--) lengths[idx++] = val; }
  }
  Huff lc, dc; build(&lc, lengths, nlen); build(&dc, lengths + nlen, ndist); codes(s, &lc, &dc);
}
uint8_t *gunzip(const uint8_t *data, size_t len, size_t *outLen) {
  if (len < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8) return NULL;
  int flg = data[3]; size_t pos = 10;
  if (flg & 4) { if (pos + 2 > len) return NULL; size_t xlen = data[pos] | (data[pos + 1] << 8); pos += 2 + xlen; }
  if (flg & 8) { while (pos < len && data[pos]) pos++; pos++; }
  if (flg & 16) { while (pos < len && data[pos]) pos++; pos++; }
  if (flg & 2) pos += 2;
  if (pos >= len) return NULL;
  St s = { data + pos, data + len - 8, 0, 0, NULL, 0, 0, 0 };
  int last;
  do {
    last = bit(&s); int type = (int)bits(&s, 2);
    if (type == 0) { s.bitbuf = 0; s.bitcnt = 0; if (s.p + 4 > s.end) { s.err = 1; break; } uint32_t n = s.p[0] | (s.p[1] << 8); s.p += 4; if (s.p + n > s.end) { s.err = 1; break; } for (uint32_t i = 0; i < n; i++) put(&s, *s.p++); }
    else if (type == 1) fixed(&s); else if (type == 2) dynamic(&s); else s.err = 1;
  } while (!last && !s.err);
  if (s.err) { free(s.out); return NULL; }
  *outLen = s.outLen; put(&s, 0); s.outLen--; // NUL-terminate for text use
  return s.out;
}
