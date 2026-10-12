function make() {
  const read = () => v;
  const early = globalThis.__probe(read);
  let v = globalThis.Number('7');
  return [early, read()];
}
export const out = make();
