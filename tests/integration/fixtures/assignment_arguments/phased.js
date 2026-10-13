export function run() {
  try {
    return globalThis.obj.f(globalThis.target.p = globalThis.rhs());
  } catch (e) {
    return globalThis.caught(e);
  }
}
