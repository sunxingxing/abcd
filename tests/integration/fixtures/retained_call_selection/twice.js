export function twice(o, g, fallback) {
  return o.call(g() || fallback, g() || fallback, o.later);
}
