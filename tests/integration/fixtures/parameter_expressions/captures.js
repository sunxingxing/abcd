export function make(M) {
  return function (a, b = M.x, c = 0) { return [a, b, c]; };
}
