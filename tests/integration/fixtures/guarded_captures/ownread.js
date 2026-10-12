export function outer() { const r = () => typeof value; return [value, r()]; }
export const early = globalThis.__probe(outer);
let value = 41;
export const late = outer();
