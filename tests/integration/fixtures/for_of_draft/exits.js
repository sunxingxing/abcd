export function scan(iterable, visit, mode) {
  let last = 17;
  for (const key of iterable) {
    visit(key);
    last = key;
    if (mode === 'break') break;
    if (mode === 'return') return key;
    if (mode === 'throw') throw new RangeError('body');
    if (mode === 'continue') continue;
  }
  return last;
}
