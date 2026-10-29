// for...of in optimized builds: the dead `done = true` store and the iterator
// close's catch store are removed, so the close handler is an empty catch at
// the shared rethrow and its guard has no state write.
export function each(xs, f) { for (const x of xs) { f(x); } }
export function total(xs) { let s = 0; for (const x of xs) { s += x; } return s; }
export function first(xs) { for (const x of xs) { if (x > 1) { return x; } } return -1; }
export function until(xs) { let s = ''; for (const x of xs) { if (x === 3) { break; } s += x; } return s; }
export function skip(xs) { let s = ''; for (const x of xs) { if (x === 2) { continue; } s += x; } return s; }
export function source(items, f) { let s = 0; for (const x of items) { s += f(x); } return s; }
