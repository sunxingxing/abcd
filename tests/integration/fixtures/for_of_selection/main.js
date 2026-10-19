export function scan(source, left, right, visit) {
  let last = null;
  for (const value of source.get()) {
    if (left(value) || !right(value)) {
      visit(value);
      last = value;
    }
  }
  return last;
}
export function outside(value, left, right, visit) {
  if (left(value) || !right(value)) visit(value);
}
