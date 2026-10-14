export const trace = [];
if (globalThis.__flag) { trace.push('before'); } else { trace.push('other'); }
class Reader { read() { return value; } }
const Copy = Reader;
trace.push(globalThis.__probe(new Copy()));
let value = globalThis.__init();
trace.push(new Copy().read());
