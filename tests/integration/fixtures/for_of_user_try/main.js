export function scan(iterable, visit, mode) {
  try {
    for (const key of iterable) {
      visit(key);
      if (mode === 'break') break;
      if (mode === 'return') return key;
      if (mode === 'throw') throw globalThis.bodyError;
    }
  } catch (error) {
    visit(error);
    return error.message;
  }
  return 17;
}
