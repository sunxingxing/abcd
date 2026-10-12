export function outer() { const w = () => { value = 5; }; w(); return value; }
export const early = globalThis.__probe(outer);
let value = 41;
export const late = outer();
