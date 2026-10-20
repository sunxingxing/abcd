export function scan(iterable, visit, mode) {
  try {
    for (const key of iterable) {
      visit(key);
      if (mode === 'break') break;
      if (mode === 'return') return key;
    }
  } catch (error) {
    visit(error.code);
    return error.message;
  }
  return 17;
}
