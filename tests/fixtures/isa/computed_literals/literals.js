export function only(k) { return { [k]: 1 }; }
export function mixed(k, x) { return { a: 1, [k]: 2, z: x }; }
export function indexed(k, x) { return { [k]: 1, 0: x, 7: 3 }; }
export function repeated(k, x) { return { [k]: x, [k]: 2 }; }
