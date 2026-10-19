export function find(obj, ...args) {
  function visit() { return args; }
  return visit(obj);
}
export function captured(obj, ...args) {
  function visit(value) { return [obj, value, args]; }
  return visit(obj);
}
export function recursive(obj, ...args) {
  function visit(n) { if (n) return visit(n - 1); return args; }
  return visit(obj);
}
export function independent(obj, ...args) {
  function first() { return args; }
  function second() { return obj; }
  return [first(), second()];
}
