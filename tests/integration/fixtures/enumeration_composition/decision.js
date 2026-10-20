export function compare(a, b, predicates) {
  if (predicates.isFunction(a) && predicates.isFunction(b)) {
    const left = a.constructor;
    const right = b.constructor;
    if (left !== right && predicates.isFunction(left) && predicates.isFunction(right) && a instanceof left && b instanceof right && !(left instanceof left && right instanceof right)) return false;
  }
  return true;
}
