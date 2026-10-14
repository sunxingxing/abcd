// Independent ordinary-object oracle. Engine input is native.abc only.
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
export function keys(object, visit) {
  let key = 'initial';
  for (key in object) {
    visit(key);
  }
  return key;
}
export function nested(outer, inner, visit) {
  for (let a in outer) for (let b in inner) visit(a, b);
}
export function keysBreak(object, visit, mode) {
  const state = {key: 'initial'};
  for (state.key in object) {
    visit(state.key);
    if (mode === 'break') break;
  }
  return state.key;
}
