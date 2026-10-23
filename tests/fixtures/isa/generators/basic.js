export function* simple() { yield 1; yield 2; return 3; }
export function* echo(n) { let s = 0; for (let i = 0; i < n; i++) { s += yield i; } return s; }
export function* delegate(it) { const r = yield* it; return r; }
