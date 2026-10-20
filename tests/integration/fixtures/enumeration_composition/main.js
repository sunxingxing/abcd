export function keys(object, visit) {
  const keys = [];
  for (const key in object) { visit(key); keys.push(key); }
  for (const key of Object.getOwnPropertySymbols(object)) {
    if (Object.prototype.propertyIsEnumerable.call(object, key)) {
      visit(key); keys.push(key);
    }
  }
  return keys;
}
