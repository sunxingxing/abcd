const escaped = [];
function make() {
  const f = () => v;
  escaped.push(f);
  let v = JSON.parse(globalThis.String('{'));
  return f;
}
export const first = globalThis.__probe(make);
export const later = globalThis.__probe(escaped[0]);
