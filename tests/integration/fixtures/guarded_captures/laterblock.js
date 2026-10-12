export function read() { return value; }
export const early = globalThis.__probe(read);
if (globalThis.Number('1')) { globalThis.__flag = globalThis.__probe(read); }
let value = 41;
export const late = read();
export const flag = globalThis.__flag;
