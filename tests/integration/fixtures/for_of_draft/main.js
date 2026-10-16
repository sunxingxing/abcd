export function scan(iterable, visit) {
  let last = 17;
  for (const key of iterable) { visit(key); last = key; }
  return last;
}
