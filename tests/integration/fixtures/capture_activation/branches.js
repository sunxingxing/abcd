export const trace = [];
if (globalThis.__flag) trace.push('before');
class Reader { read() { return value; } }
trace.push(globalThis.__probe(new Reader()));
class Other { read() { return 2; } }
trace.push(new Other().read());
let value = globalThis.__init();
trace.push(new Reader().read());
export const final = new Reader();
