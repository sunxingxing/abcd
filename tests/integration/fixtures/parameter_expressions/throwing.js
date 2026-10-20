export function f(a, b = globalThis.input.x) {
  globalThis.log.push('body');
  return [a, b];
}
