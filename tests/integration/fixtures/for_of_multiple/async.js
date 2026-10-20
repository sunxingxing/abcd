export async function scan(iterable, visit, mode) {
  for (const key of iterable) {
    await visit(key);
    if (mode === 'break') break;
    if (mode === 'return') return key;
  }
  return 17;
}
