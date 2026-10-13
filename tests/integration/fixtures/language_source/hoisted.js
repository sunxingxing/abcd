const scale = globalThis.SCALE;
let offset = globalThis.OFFSET;
function twice() { return scale * 2; }
export function shifted(v) { return v + offset + scale; }
function bump() { offset = offset + 1; return offset; }
export const result = twice() + ',' + shifted(1) + ',' + bump() + ',' + shifted(1);
