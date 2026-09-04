// Reads the clock's 7-segment display from a Hashlife universe of clock.rle.
// A lit segment is a bundle of glider streams along the segment; the corner
// hardware is present either way, so every sample rectangle stays clear of
// the corners. Coordinates are relative to the pattern's top-left.
module.exports = function makeReader(u, X0, Y0) {
  const count = (x0, y0, w, h) => { const bm = u.bitmap(X0 + x0, Y0 + y0, w, h); let n = 0; for (let i = 0; i < bm.length; i++) n += bm[i]; return n; };
  const BOX = { ho: [3080, 4720], mt: [5880, 7320], mo: [8200, 9720] }, HT = [2040, 2400];
  const TH = 30;
  function segs([x0, x1]) {
    const xm = (x0 + x1) >> 1;
    // Horizontal segments: the middle 600 cells only; the corner hardware
    // reaches ~450 cells in from each end of the box.
    return { a: count(xm - 300, 4130, 600, 200), g: count(xm - 300, 5330, 600, 200), d: count(xm - 300, 6530, 600, 200),
             f: count(x0, 4500, xm - x0, 650), b: count(xm, 4500, x1 - xm, 650), e: count(x0, 5700, xm - x0, 600), c: count(xm, 5700, x1 - xm, 600) };
  }
  const PAT = { abcdef: '0', bc: '1', abdeg: '2', abcdg: '3', bcfg: '4', acdfg: '5', acdefg: '6', abc: '7', abcdefg: '8', abcdfg: '9', '': ' ' };
  function digit(box) { const s = segs(box); const on = 'abcdefg'.split('').filter(k => s[k] > TH).join(''); return PAT[on] !== undefined ? PAT[on] : '?' + on + '?'; }
  function read() { return (count(HT[0], 4500, HT[1] - HT[0], 1800) > TH ? '1' : ' ') + digit(BOX.ho) + ':' + digit(BOX.mt) + digit(BOX.mo); }
  return { read, segs, BOX };
};
