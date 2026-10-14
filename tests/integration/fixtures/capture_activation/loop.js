export const trace = [];
export const readers = [];
while (globalThis.__again()) {
  if (globalThis.__flag) trace.push('iteration');
  class Reader { read() { return value; } }
  trace.push(globalThis.__probe(new Reader()));
  let value = globalThis.__init();
  readers.push(new Reader());
}
