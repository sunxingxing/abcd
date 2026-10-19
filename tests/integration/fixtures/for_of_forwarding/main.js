export function scan(source, visit) {
  let last = 17;
  for (const key of source.get()) { visit(key); last = key; }
  return last;
}
