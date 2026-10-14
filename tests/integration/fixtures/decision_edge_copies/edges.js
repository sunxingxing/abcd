export function check(o) {
  let value = o.initial;
  if (o.a || o.b) { value = o.value; o.effect; }
  return value;
}
export function andCheck(o) {
  let value = o.initial;
  if (o.a && o.b) { value = o.value; o.effect; }
  return value;
}
export function handled(o) {
  try {
    let value = o.initial;
    if (o.a || o.b) { value = o.value; o.effect; }
    return value;
  } catch (e) { o.caught(e.constructor.name); return 'caught'; }
}
export function swap(o) {
  let a = o.initial, b = o.other;
  while (o.more) {
    if (o.a || o.b) { const t = a; a = b; b = t; o.effect; }
    o.observe(a, b);
  }
  return [a, b];
}
