let base = 3;
export function make(k) {
  const o = { a: 1, add(r) { return r + k + base; }, b: "x", self() { return this.a; } };
  return o;
}
export function capture(resolve, reject) {
  const handler = { onResult(r) { if (r.ok) resolve(r.v); else reject(r.v); } };
  return handler;
}
export function guarded(list) {
  try {
    const box = { take(i) { return list[i]; } };
    return box.take(0);
  } catch (e) {
    return -1;
  }
}
export const plain = { m(x, y) { return x * y; }, n: 2 };
export const asyncs = { async fetch(v) { return v + 1; } };
export const gens = { *range(n) { for (let i = 0; i < n; i++) yield i; } };
export function nested(v) {
  return { outer() { return { inner() { return v; } }; } };
}
