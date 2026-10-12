export function outer() { return () => { value = 1; }; }
export const early = globalThis.__probe(() => outer()());
let value = 41;
export function get() { return value; }
