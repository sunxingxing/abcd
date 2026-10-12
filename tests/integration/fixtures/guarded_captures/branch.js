export const out = [];
if (globalThis.Number('1')) {
  function f() { return k; }
  out.push(globalThis.__probe(f));
  let k = 7;
  out.push(f());
}
