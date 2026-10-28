import { helper, count, Box } from 'data:text/javascript,export function helper(x){return globalThis.__importProbe(x)}export let count=1;export class Box{constructor(v){this.v=v}}';
export function call(x) {
  try { return helper(x); } catch (e) { return ['caught', e]; }
}
export function alias(x) {
  let h = x;
  try { h = helper; return h(x); } catch (e) { return ['kept', h === x]; }
}
export function arithmetic(x) {
  let total = 0;
  try { total = count + x; helper(total); } catch (e) { total = -total; }
  return total;
}
export function construct(x) {
  try { return new Box(helper(x)).v; } catch (e) { return new Box(e).v; }
}
