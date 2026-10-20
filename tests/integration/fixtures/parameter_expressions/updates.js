export function make(M) {
  return function (a, b = M.x) {
    if (a === 99) M = globalThis.replacement;
    return [a, b];
  };
}
