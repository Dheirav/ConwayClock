// Generate a snapshot of the clock every SNAP_MIN displayed minutes across
// its 12-hour cycle. Each snapshot is the exact state at generation
// m * 11520 (m = displayed minutes since 12:00 PM), written as gzip'd RLE.
// Progress goes to snapshots/progress.json; watch with tools/snapshot-progress.sh.
require('./hashlife.js');
const { Universe, parseRLE, toRLE } = Hashlife;
const fs = require('fs'), path = require('path'), zlib = require('zlib');
// START/END (minutes) select a range of the 24-hour cycle; default first 12 h.
const PERIOD = 11520, SNAP_MIN = 10, START = parseInt(process.env.START || '0', 10), END = parseInt(process.env.END || '720', 10), TOTAL = (END - START) / SNAP_MIN;
const dir = path.join(__dirname, '..', 'snapshots');
const c = Universe.fromCells(parseRLE(fs.readFileSync(path.join(__dirname, '..', 'clock.rle'), 'utf8')));
const R = require('./reader.js')(c, c.x0, c.y0);
const started = Date.now(); const times = [];
function progress(done, note) {
  fs.writeFileSync(path.join(dir, 'progress.json'), JSON.stringify({ done, total: TOTAL, started, updated: Date.now(), seconds: times, note }));
}
function cellsOf(u) {
  const bm = u.bitmap(u.x0, u.y0, u.size, u.size); const out = [];
  for (let y = 0; y < u.size; y++) for (let x = 0; x < u.size; x++) if (bm[y * u.size + x]) out.push([u.x0 + x, u.y0 + y]);
  return out;
}
if (START > 0) { const t0 = Date.now(); c.advance(PERIOD * START); console.log(`advanced to minute ${START} in ${((Date.now() - t0) / 1000).toFixed(1)} s`); }
for (let i = 0; i < TOTAL; i++) {
  const t0 = Date.now();
  if (i > 0) c.advance(PERIOD * SNAP_MIN);
  const cells = cellsOf(c);
  // Store absolute coordinates so every snapshot lines up with clock.rle.
  let minx = Infinity, miny = Infinity; for (const [x, y] of cells) { if (x < minx) minx = x; if (y < miny) miny = y; }
  const rle = `#CXRLE Pos=${minx},${miny}\n` + toRLE(cells);
  fs.writeFileSync(path.join(dir, `m${String(START + i * SNAP_MIN).padStart(4, '0')}.rle.gz`), zlib.gzipSync(rle, { level: 9 }));
  const disp = R.read();
  times.push((Date.now() - t0) / 1000);
  const note = `snapshot ${i}: minute ${START + i * SNAP_MIN}, gen ${c.generation}, display "${disp}", pop ${c.population()}, ${cells.length} cells, ${times[times.length - 1].toFixed(1)} s`;
  console.log(note); progress(i + 1, note);
  if (i % 6 === 5) { const g = c.gc(); console.log('  gc', JSON.stringify(g), 'rss', (process.memoryUsage().rss / 1e6).toFixed(0), 'MB'); }
}
console.log('done');
