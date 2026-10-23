export function f(log) { const k = 1; log.push("a"); k = log.length; return k; }
export function g() { try { const c = 0; c = 1; return "no"; } catch (e) { return e instanceof TypeError; } }
export function h(o) {
  let r = 0;
  const c = 1;
  try { c = o.v; } catch (e) { r = e.constructor === TypeError; }
  return [r, c];
}
