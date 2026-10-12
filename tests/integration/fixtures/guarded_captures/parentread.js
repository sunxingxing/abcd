export function read() { return value; }
export const p = globalThis.__probe(() => value);
let value = 41;
