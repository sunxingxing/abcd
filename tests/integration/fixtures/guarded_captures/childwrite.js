export function set(v) { value = v; }
export const early = globalThis.__probe(() => set(3));
let value = 41;
export function get() { return value; }
