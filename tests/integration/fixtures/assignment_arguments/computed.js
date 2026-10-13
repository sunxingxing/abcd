export function run() {
  const key = globalThis.key();
  const value = globalThis.rhs();
  return globalThis.obj.f(globalThis.target[key] = value);
}
