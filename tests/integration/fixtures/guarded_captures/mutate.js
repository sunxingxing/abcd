export function get() { return count; }
export const p0 = globalThis.__probe(get);
let count = globalThis.Number('5');
count = count + 1;
export const after = get();
