const seen = [];
function make(n) {
  const f = () => v;
  seen.push(globalThis.__probe(f));
  if (n > 0) seen.push(globalThis.__probe(make(n - 1)));
  let v = n * 10;
  return f;
}
export const top = globalThis.__probe(make(2));
export const log = seen;
