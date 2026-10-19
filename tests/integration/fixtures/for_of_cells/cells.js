export function scan(iterable, visit) {
  let sum = 0;
  const add = function (value) { sum = sum + value; };
  for (const key of iterable) {
    add(key);
    visit(key, sum);
  }
  return sum;
}
