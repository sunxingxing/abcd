export function outer() { const f = () => value; return [f, () => value + 1]; }
export const early = globalThis.__probe(() => outer()[1]());
let value = 41;
export const late = outer()[0]() + outer()[1]();
