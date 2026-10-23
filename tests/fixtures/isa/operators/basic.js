export function arith(a, b) { return [a % b, a <= b, a > b, a >= b, a << b, a >> b, a >>> b, a & b, a | b]; }
export function unary(a) { return [+a, !a, -a, ~a, typeof a]; }
export function truthy(a) { let n = 0; if (a) { n += 1; } if (!a) { n += 2; } while (a && n < 5) { n += 3; } return n; }
export function relation(k, o, C) { return [k in o, o instanceof C]; }
export function consts() { return [NaN, Infinity, -Infinity, 0 / 0]; }
export function spreadCall(f, a) { return f(...a); }
export function manyArgs(f) { return f(1, 2, 3, 4, 5, 6); }
export function manyThis(o) { return o.m(1, 2, 3, 4, 5, 6); }
export function named() { const o = { f: function () { return 1; }, g: () => 2 }; return [o.f.name, o.g.name, o.f(), o.g()]; }
export function dbg(x) { debugger; return x; }
