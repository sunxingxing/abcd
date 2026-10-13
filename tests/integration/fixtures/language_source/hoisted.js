const scale = globalThis.SCALE;
let offset = globalThis.CALL(() => globalThis.OFFSET);
function twice() { return scale * 2; }
export function shifted(v) { return v + offset + scale; }
offset = offset + 1;
export const result = twice() + ',' + shifted(1);
