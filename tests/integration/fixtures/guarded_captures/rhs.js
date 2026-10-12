export function read() { return value; }
let value = globalThis.__probe(read);
export const seen = value;
export const again = read();
