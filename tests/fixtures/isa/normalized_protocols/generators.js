export function* before(xs) {
  const [x] = xs;
  yield x;
  return x;
}
export function* between(xs) {
  yield 'first';
  const [x] = xs;
  yield x;
  return x;
}
export function* after(xs) {
  yield 'first';
  const [x] = xs;
  return x;
}
export function* caught(xs) {
  try { const [x] = xs; yield x; return x; }
  catch (e) { return e; }
}
