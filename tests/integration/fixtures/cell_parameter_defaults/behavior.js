export function make(seed, log) {
  let outer = seed;
  function f(a, b, c = outer) {
    log.push('body');
    return {get: () => c, set: value => { c = value; }};
  }
  return {f, set: value => { outer = value; }};
}
export function early(log, provided) {
  function f(a, b, c = outer) {
    log.push('body');
    return () => c;
  }
  let result = provided ? f(0, 0, 3) : f(0, 0);
  let outer = 9;
  return result;
}
