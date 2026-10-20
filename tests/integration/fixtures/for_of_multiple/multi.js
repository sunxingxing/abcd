export function scan(iterable, visit, mode) {
  for (const key of iterable) {
    visit(key);
    if (mode === 'break') break;
    if (mode === 'return') return key;
    if (mode === 'throw') throw globalThis.bodyError;
    if (mode === 'continue') continue;
    visit(mode);
  }
  return 17;
}
