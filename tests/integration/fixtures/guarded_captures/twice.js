export function sum() { return a + b; }
export const p1 = globalThis.__probe(sum);
let a = 1;
export const p2 = globalThis.__probe(sum);
let b = globalThis.Number('2');
export const total = sum();
