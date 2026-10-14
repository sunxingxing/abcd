export function direct(o, g, fallback) {
  return o.call(o.earlier, g() || fallback, o.later);
}
export function copied(o, g, fallback) {
  const saved = fallback;
  return o.call(o.earlier, g() || saved, o.later);
}
export function and(o, g, fallback) {
  return o.call(o.earlier, g() && fallback, o.later);
}
export function nested(o, g, fallback, h) {
  return o.call(o.earlier, h(g() || fallback), o.later);
}
