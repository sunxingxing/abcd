export function* plain(a, b, c = 8) { yield c; return [a, b, c]; }
export function* multiple(a, b, c = 8, d = false) { yield c; yield d; return b; }
export function* captured(a, b, c = 8) { const read = () => c; yield read(); return [read(), b]; }
