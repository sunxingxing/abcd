export function cells(n, effect) {
  let value = effect('init', 3);
  const read = () => value;
  let total = 0;
  while (n-- > 0) {
    value = effect('write', read() + 1);
    total += value;
  }
  return [total, read()];
}
export function activation(n, effect) {
  const saved = [];
  while (n-- > 0) {
    let value = effect('init', n);
    saved.push(() => value);
    value = effect('write', value + 1);
  }
  return saved.map(f => f());
}
