// Value-producing && / || conditions inside protected regions (user try and
// async bodies), including throwing operands under non-throwing comparisons
// and the single-pass for-of with array pattern inside a user try.
export function guarded(a, c, d, g, h) {
  try { if (a() || (c() === 2 && d)) g(); } catch (e) { h(e); }
}
export function throwingOperand(x, y, g, h) {
  try { if (x.a() || (x.t() === 2 && y.d === x)) { g(); } } catch (e) { h(e); }
}
export async function asyncCompound(x, y, g) {
  if ((x.t() === 1 && x.i(y)) || (x.t() === 2 && x === y)) { await g(); }
}
export async function asyncThrowingOperand(xs, y, g) {
  for (const x of xs) { if (x.a() || (x.t() === 2 && x === y.d)) { await g(x); } }
}
export async function nestedPattern(specs, map, fn, log) {
  for (const spec of specs) {
    try {
      for (const [k, x] of Object.entries(map)) {
        if ((Object.prototype.toString.call(x) === '[object Array]' && x.includes(spec.description)) ||
            (Object.prototype.toString.call(x) === '[object String]' && x === spec.description)) {
          await Reflect.apply(fn, null, [k]);
        }
        break;
      }
    } catch (e) { log(`stack:${e?.stack}`); }
  }
}
