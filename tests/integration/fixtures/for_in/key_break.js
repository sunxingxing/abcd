// Negative boundary: exhaustion copies may not execute after an early break.
export function keyBreak(object, visit, stop) {
  let key = 'initial';
  for (key in object) {
    visit(key);
    if (stop) break;
  }
  return key;
}
