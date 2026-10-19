export function find(obj, ...args) {
  function visit(x) { return x.value; }
  return [visit(obj), args];
}
export function captured(obj, ...args) {
  function visit() { return obj.value; }
  return [visit(), args];
}
export function recursive(obj, ...args) {
  function visit(n) { if (n) return visit(n - 1); return obj.value; }
  return [visit(obj.depth), args];
}
