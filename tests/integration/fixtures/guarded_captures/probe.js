export function read() { return value; }
export const early = globalThis.__probe(read);
let value = 41;
export const late = read();
