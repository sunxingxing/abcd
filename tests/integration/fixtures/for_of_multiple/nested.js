export function scan(iterable, visit, mode) {
  for (const a of iterable) {
    for (const b of globalThis.innerIterable) {
      visit(a, b);
      if (mode === 'return') return b;
      if (mode === 'throw') throw globalThis.bodyError;
      break;
    }
  }
  return 17;
}
