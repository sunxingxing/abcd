export function pair(p) { const [a, b] = p; return [a, b]; }
export function holes(p) { let [a, , b] = p; return [a, b]; }
export function trailing(p) { const [a, ,] = p; return a; }
export function reassign(p, q) { let a = 0, b = 0; [a, b] = p; const r = [a, b]; [b] = q; return [r, a, b]; }
export function swap(p) { let a = 1, b = 2; [a, b] = [b, a] = p; return [a, b]; }
export function guarded(p) {
  try { const [x, y] = p; return x * y; } catch (e) { return 'caught:' + (e instanceof TypeError ? 'TypeError' : e.message); }
}
export function looped(source) {
  let total = 0;
  while (source.more()) { const [k, v] = source.next(); if (k) total += v; }
  return total;
}
export async function awaited(p) { const [a, b] = await p; return a + b; }
