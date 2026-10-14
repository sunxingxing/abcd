export function factory() {
  const C = class { read() { return value; } };
  let value = 1;
  return C;
}
