// Evaluation oracle only. Production input is loops.abc.
export function scan(object, visit) {
  for (let key in object) visit(key, object[key]);
}
export function exits(object, visit, mode) {
  for (let key in object) {
    visit(key);
    if (mode === 'continue') continue;
    if (mode === 'break') break;
    if (mode === 'return') return key;
    if (mode === 'throw') throw key;
  }
  return 'end';
}
export function captured(object, save) {
  for (let key in object) save(() => key);
}
export function nested(outer, inner, visit) {
  for (let a in outer) for (let b in inner) visit(a, b);
}
