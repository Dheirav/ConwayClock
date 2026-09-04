// Hashlife: a memoised quadtree Life engine (Gosper's algorithm). Plain
// script, works in Node and in a browser. Exposes a global `Hashlife`.
//
// A node of level L covers a 2^L square. Leaves (level 0) are the two cells
// OFF (0) and ON (1). Every node is canonical (hash-consed), so identical
// regions share memory, and each (node, j) pair caches the node's centre
// advanced 2^j generations. That cache is what makes huge periodic machines
// like the digital clock cheap to run.
//
// Nodes live in parallel typed arrays rather than objects: roughly 40 bytes
// per node instead of several hundred, which is the difference between a
// wallpaper and a memory hog.
(function (root) {
'use strict';

let cap = 1 << 18;
let A = new Int32Array(cap), B = new Int32Array(cap), C = new Int32Array(cap), D = new Int32Array(cap);
let POP = new Int32Array(cap), LEVEL = new Uint8Array(cap);
let count = 2;                       // 0 = OFF, 1 = ON
POP[1] = 1;
let HT = new Int32Array(cap * 2).fill(-1), htMask = cap * 2 - 1;   // node hash table (open addressing)
let RK = new Int32Array(cap * 2).fill(-1), RV = new Int32Array(cap * 2), rMask = cap * 2 - 1, rCount = 0; // (node, j) -> result
const emptyCache = [0];
const tiles = new Map();

const hash4 = (a, b, c, d) => (Math.imul(a, 0x9E3779B1) ^ Math.imul(b, 0x85EBCA77) ^ Math.imul(c, 0xC2B2AE3D) ^ Math.imul(d, 0x27D4EB2F)) >>> 0;

function growNodes() {
  cap *= 2;
  const g = (arr, T) => { const n = new T(cap); n.set(arr); return n; };
  A = g(A, Int32Array); B = g(B, Int32Array); C = g(C, Int32Array); D = g(D, Int32Array); POP = g(POP, Int32Array); LEVEL = g(LEVEL, Uint8Array);
  rehashNodes();
}
function rehashNodes() {
  HT = new Int32Array(cap * 2).fill(-1); htMask = cap * 2 - 1;
  for (let n = 2; n < count; n++) { let i = hash4(A[n], B[n], C[n], D[n]) & htMask; while (HT[i] !== -1) i = (i + 1) & htMask; HT[i] = n; }
}
function join(a, b, c, d) {
  let i = hash4(a, b, c, d) & htMask;
  for (;;) {
    const n = HT[i];
    if (n === -1) break;
    if (A[n] === a && B[n] === b && C[n] === c && D[n] === d) return n;
    i = (i + 1) & htMask;
  }
  if (count >= cap - 1) { growNodes(); return join(a, b, c, d); }
  const n = count++;
  A[n] = a; B[n] = b; C[n] = c; D[n] = d; POP[n] = POP[a] + POP[b] + POP[c] + POP[d]; LEVEL[n] = LEVEL[a] + 1;
  HT[i] = n;
  return n;
}
function empty(level) {
  while (emptyCache.length <= level) { const e = emptyCache[emptyCache.length - 1]; emptyCache.push(join(e, e, e, e)); }
  return emptyCache[level];
}
function expand(n) { const e = empty(LEVEL[n] - 1); return join(join(e, e, e, A[n]), join(e, e, B[n], e), join(e, C[n], e, e), join(D[n], e, e, e)); }

function resGet(n, j) {
  const key = n * 32 + j; let i = (Math.imul(key, 0x9E3779B1) >>> 0) & rMask;
  for (;;) { const k = RK[i]; if (k === key) return RV[i]; if (k === -1) return -1; i = (i + 1) & rMask; }
}
function resPut(n, j, v) {
  if (rCount * 10 > (rMask + 1) * 7) growRes();
  const key = n * 32 + j; let i = (Math.imul(key, 0x9E3779B1) >>> 0) & rMask;
  while (RK[i] !== -1 && RK[i] !== key) i = (i + 1) & rMask;
  if (RK[i] === -1) rCount++;
  RK[i] = key; RV[i] = v;
}
function growRes() {
  const ok = RK, ov = RV; const size = (rMask + 1) * 2;
  RK = new Int32Array(size).fill(-1); RV = new Int32Array(size); rMask = size - 1; rCount = 0;
  for (let i = 0; i < ok.length; i++) if (ok[i] !== -1) { let p = (Math.imul(ok[i], 0x9E3779B1) >>> 0) & rMask; while (RK[p] !== -1) p = (p + 1) & rMask; RK[p] = ok[i]; RV[p] = ov[i]; rCount++; }
}

const bits = new Uint8Array(16);
function life4x4(n) {
  const sub = [A[n], B[n], C[n], D[n]];
  for (let q = 0; q < 4; q++) {
    const s = sub[q], ox = (q & 1) * 2, oy = (q >> 1) * 2;
    bits[oy * 4 + ox] = POP[A[s]]; bits[oy * 4 + ox + 1] = POP[B[s]]; bits[(oy + 1) * 4 + ox] = POP[C[s]]; bits[(oy + 1) * 4 + ox + 1] = POP[D[s]];
  }
  const cell = (x, y) => {
    let k = 0;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) if (dx || dy) k += bits[(y + dy) * 4 + x + dx];
    return (k === 3 || (k === 2 && bits[y * 4 + x])) ? 1 : 0;
  };
  const r = [cell(1, 1), cell(2, 1), cell(1, 2), cell(2, 2)];
  return join(r[0], r[1], r[2], r[3]);
}
const centre = n => join(D[A[n]], C[B[n]], B[C[n]], A[D[n]]);
const horiz = (w, e) => join(B[w], A[e], D[w], C[e]);
const vert = (n, s) => join(C[n], D[n], A[s], B[s]);

// Centre of `n` advanced by 2^j generations (j <= level-2).
function successor(n, j) {
  if (POP[n] === 0) return empty(LEVEL[n] - 1);
  if (LEVEL[n] === 2) return life4x4(n);
  const hit = resGet(n, j); if (hit !== -1) return hit;
  const a = A[n], b = B[n], c = C[n], d = D[n];
  const c1 = successor(a, j), c2 = successor(horiz(a, b), j), c3 = successor(b, j);
  const c4 = successor(vert(a, c), j), c5 = successor(centre(n), j), c6 = successor(vert(b, d), j);
  const c7 = successor(c, j), c8 = successor(horiz(c, d), j), c9 = successor(d, j);
  let r;
  if (j < LEVEL[n] - 2) r = join(centre(join(c1, c2, c4, c5)), centre(join(c2, c3, c5, c6)), centre(join(c4, c5, c7, c8)), centre(join(c5, c6, c8, c9)));
  else r = join(successor(join(c1, c2, c4, c5), j), successor(join(c2, c3, c5, c6), j), successor(join(c4, c5, c7, c8), j), successor(join(c5, c6, c8, c9), j));
  resPut(n, j, r);
  return r;
}

class Universe {
  constructor() { this.root = empty(3); this.x0 = -4; this.y0 = -4; this.generation = 0; }
  get level() { return LEVEL[this.root]; }
  get size() { return 1 << LEVEL[this.root]; }
  static fromCells(cells) {
    const u = new Universe();
    if (!cells.length) return u;
    let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
    for (const [x, y] of cells) { if (x < minx) minx = x; if (y < miny) miny = y; if (x > maxx) maxx = x; if (y > maxy) maxy = y; }
    let level = 1; while ((1 << level) < Math.max(maxx - minx + 1, maxy - miny + 1)) level++;
    const build = (list, lvl, ox, oy) => {
      if (!list.length) return empty(lvl);
      if (lvl === 0) return 1;
      const half = 1 << (lvl - 1), q = [[], [], [], []];
      for (const c of list) q[((c[1] - oy) >= half ? 2 : 0) + ((c[0] - ox) >= half ? 1 : 0)].push(c);
      return join(build(q[0], lvl - 1, ox, oy), build(q[1], lvl - 1, ox + half, oy), build(q[2], lvl - 1, ox, oy + half), build(q[3], lvl - 1, ox + half, oy + half));
    };
    u.root = build(cells, level, minx, miny); u.x0 = minx; u.y0 = miny;
    return u;
  }
  _expand() { this.root = expand(this.root); this.x0 -= this.size >> 2; this.y0 -= this.size >> 2; }
  _trim() {
    while (LEVEL[this.root] > 3) {
      const r = this.root, q = this.size >> 2;
      if (POP[D[A[r]]] + POP[C[B[r]]] + POP[B[C[r]]] + POP[A[D[r]]] !== POP[r]) break;
      this.root = centre(r); this.x0 += q; this.y0 += q;
    }
  }
  // Advance by exactly `gens` generations.
  advance(gens) {
    for (let j = 0; (1 << j) <= gens; j++) if (gens & (1 << j)) {
      // The result is the centre half of the root advanced 2^j generations.
      // Cells move at most 2^j, so it is exact when everything alive sits in
      // the central quarter and that quarter is at least 2^j from the
      // result's edge: level >= j+3.
      while (LEVEL[this.root] < j + 3 || POP[centre(centre(this.root))] !== POP[this.root]) this._expand();
      const q = this.size >> 2;
      this.root = successor(this.root, j);
      this.x0 += q; this.y0 += q;
      this.generation += 1 << j;
    }
    this._trim();
  }
  population() { return POP[this.root]; }
  bitmap(x, y, w, h) {
    const out = new Uint8Array(w * h);
    const rec = (n, nx, ny) => {
      if (POP[n] === 0) return;
      const s = 1 << LEVEL[n];
      if (nx + s <= x || ny + s <= y || nx >= x + w || ny >= y + h) return;
      if (LEVEL[n] === 0) { out[(ny - y) * w + (nx - x)] = 1; return; }
      const half = s >> 1;
      rec(A[n], nx, ny); rec(B[n], nx + half, ny); rec(C[n], nx, ny + half); rec(D[n], nx + half, ny + half);
    };
    rec(this.root, this.x0, this.y0);
    return out;
  }
  // Downsampled view: each output pixel covers a 2^z square; value = live count (capped 255).
  density(x, y, w, h, z) {
    const out = new Uint8Array(w * h), blk = 1 << z;
    const rec = (n, nx, ny) => {
      if (POP[n] === 0) return;
      const s = 1 << LEVEL[n];
      if (nx + s <= x || ny + s <= y || nx >= x + w * blk || ny >= y + h * blk) return;
      if (LEVEL[n] <= z) { const i = ((ny - y) >> z) * w + ((nx - x) >> z); out[i] = Math.min(255, out[i] + POP[n]); return; }
      const half = s >> 1;
      rec(A[n], nx, ny); rec(B[n], nx + half, ny); rec(C[n], nx, ny + half); rec(D[n], nx + half, ny + half);
    };
    rec(this.root, this.x0, this.y0);
    return out;
  }
  // Same, with per-node tile caching at `tileLevel`, so a periodic machine
  // renders almost entirely from cache. Tiles are dropped by gc().
  densityCached(x, y, w, h, z, tileLevel = z + 3) {
    const out = new Uint8Array(w * h), blk = 1 << z, ts = 1 << (tileLevel - z);
    const tile = n => {
      let t = tiles.get(n); if (t) return t;
      t = new Uint8Array(ts * ts);
      const rec = (m, mx, my) => {
        if (POP[m] === 0) return;
        if (LEVEL[m] <= z) { const i = (my >> z) * ts + (mx >> z); t[i] = Math.min(255, t[i] + POP[m]); return; }
        const half = 1 << (LEVEL[m] - 1);
        rec(A[m], mx, my); rec(B[m], mx + half, my); rec(C[m], mx, my + half); rec(D[m], mx + half, my + half);
      };
      rec(n, 0, 0); tiles.set(n, t); return t;
    };
    const rec = (n, nx, ny) => {
      if (POP[n] === 0) return;
      const s = 1 << LEVEL[n];
      if (nx + s <= x || ny + s <= y || nx >= x + w * blk || ny >= y + h * blk) return;
      if (LEVEL[n] === tileLevel) {
        const t = tile(n), px = (nx - x) >> z, py = (ny - y) >> z;
        for (let ty = 0; ty < ts; ty++) { const oy = py + ty; if (oy < 0 || oy >= h) continue;
          for (let tx = 0; tx < ts; tx++) { const ox = px + tx; if (ox >= 0 && ox < w) out[oy * w + ox] = t[ty * ts + tx]; } }
        return;
      }
      const half = s >> 1;
      rec(A[n], nx, ny); rec(B[n], nx + half, ny); rec(C[n], nx, ny + half); rec(D[n], nx + half, ny + half);
    };
    rec(this.root, this.x0, this.y0);
    return out;
  }
  // Compact: keep only nodes reachable from the root (and memo entries whose
  // key and value both survive), renumber, rebuild the tables.
  gc() {
    const before = count;
    const mark = new Uint8Array(count); mark[0] = mark[1] = 1;
    const stack = [this.root];
    while (stack.length) { const n = stack.pop(); if (mark[n]) continue; mark[n] = 1; if (LEVEL[n] > 0) stack.push(A[n], B[n], C[n], D[n]); }
    // Memo entries are worth keeping only if both ends are marked; but the
    // value nodes must then be marked too (transitively), so iterate.
    let added = true;
    while (added) { added = false;
      for (let i = 0; i < RK.length; i++) { const k = RK[i]; if (k === -1) continue; const n = Math.floor(k / 32), v = RV[i];
        if (mark[n] && !mark[v]) { const st = [v]; while (st.length) { const m = st.pop(); if (mark[m]) continue; mark[m] = 1; added = true; if (LEVEL[m] > 0) st.push(A[m], B[m], C[m], D[m]); } } } }
    const map = new Int32Array(count).fill(-1); let k = 0;
    for (let n = 0; n < count; n++) if (mark[n]) map[n] = k++;
    const nA = new Int32Array(cap), nB = new Int32Array(cap), nC = new Int32Array(cap), nD = new Int32Array(cap), nP = new Int32Array(cap), nL = new Uint8Array(cap);
    for (let n = 0; n < count; n++) if (mark[n]) { const m = map[n]; nA[m] = LEVEL[n] ? map[A[n]] : 0; nB[m] = LEVEL[n] ? map[B[n]] : 0; nC[m] = LEVEL[n] ? map[C[n]] : 0; nD[m] = LEVEL[n] ? map[D[n]] : 0; nP[m] = POP[n]; nL[m] = LEVEL[n]; }
    A = nA; B = nB; C = nC; D = nD; POP = nP; LEVEL = nL; count = k;
    rehashNodes();
    const oK = RK, oV = RV;
    RK = new Int32Array(oK.length).fill(-1); RV = new Int32Array(oK.length); rCount = 0;
    for (let i = 0; i < oK.length; i++) { const key = oK[i]; if (key === -1) continue; const n = Math.floor(key / 32), j = key % 32; if (mark[n] && mark[oV[i]]) resPut(map[n], j, map[oV[i]]); }
    emptyCache.length = 1; // rebuilt lazily; unreachable empties were dropped
    tiles.clear();
    this.root = map[this.root];
    return { before, after: count, memo: rCount };
  }
  static stats() { return { nodes: count, cap, memo: rCount, tiles: tiles.size, bytes: cap * (4 * 4 + 4 + 1) + HT.length * 4 + RK.length * 8 }; }
}

function parseRLE(text) {
  const cells = []; let x = 0, y = 0;
  const lines = text.split(/\r?\n/).filter(l => l && l[0] !== '#' && !/^x\s*=/.test(l));
  const re = /(\d*)([bo$!])/g; let m; const body = lines.join('');
  while ((m = re.exec(body))) {
    const n = m[1] ? parseInt(m[1], 10) : 1, t = m[2];
    if (t === 'b') x += n; else if (t === 'o') { for (let i = 0; i < n; i++) cells.push([x++, y]); } else if (t === '$') { y += n; x = 0; } else break;
  }
  return cells;
}
// Encode a bitmap region as RLE text (for snapshots).
function toRLE(cells) {
  cells.sort((p, q) => p[1] - q[1] || p[0] - q[0]);
  let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
  for (const [x, y] of cells) { if (x < minx) minx = x; if (y < miny) miny = y; if (x > maxx) maxx = x; if (y > maxy) maxy = y; }
  const out = [`x = ${maxx - minx + 1}, y = ${maxy - miny + 1}, rule = B3/S23`]; let line = '', cy = miny, cx = minx;
  const emit = (n, t) => { line += (n > 1 ? n : '') + t; if (line.length > 68) { out.push(line); line = ''; } };
  let run = 0;
  for (const [x, y] of cells) {
    if (y !== cy) { if (run) { emit(run, 'o'); run = 0; } emit(y - cy, '$'); cy = y; cx = minx; }
    if (x > cx) { if (run) { emit(run, 'o'); run = 0; } emit(x - cx, 'b'); cx = x; }
    run++; cx++;
  }
  if (run) emit(run, 'o'); emit(1, '!'); out.push(line);
  return out.join('\n');
}

root.Hashlife = { Universe, parseRLE, toRLE };
})(typeof globalThis !== 'undefined' ? globalThis : this);
