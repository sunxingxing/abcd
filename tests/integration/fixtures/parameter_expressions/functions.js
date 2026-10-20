import { M } from './m.js';
export function f(a, b = M.x, c = 0, d = -1, e = globalThis.y) {
  return [a, b, c, d, e];
}
