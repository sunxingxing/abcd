export function prefix(f, o) { return f(++o.p); }
export function postfix(f, o) { return f(o.p++); }
export function compound(f, o, k, g) { return f(o[k()] += g()); }
export function savedMember(f, o) { return f.m(++o.p); }
export function branches(x, c, A, B, source) {
  return x.create(c ? new A({a: [source.a], q: source.g()}) : new B({b: [source.b], q: source.g()}));
}
export function simple(x, c, A, B) {
  return x.create(c ? new A({a: 1}) : new B([1, 2]));
}
export function nested(x, c, A, B, source) {
  return x.create(c ? new A({space: source.vp(10)}) : new B({space: source.vp(10)}));
}
