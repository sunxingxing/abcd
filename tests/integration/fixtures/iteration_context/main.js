export function scan(object, source, visit) {
  const result = [];
  let key;
  for (key in object) result.push(visit(key));
  let value;
  for (value of source.get()) result.push(visit(value));
  return [result, key, value];
}
