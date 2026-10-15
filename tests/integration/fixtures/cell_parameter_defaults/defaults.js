export function make(seed) {
  let outer = seed;
  return function f(a, b, c = outer) {
    return () => c;
  };
}
