export function copy(source) { return {before: 1, ...source, after: 2}; }
export function multiple(a, b) { return {...a, fixed: 7, ...b}; }
export function ordered(first, source, last) { return {a: first(), ...source(), z: last()}; }
export function nested(source) { return {outer: {head: 0, ...source, tail: 3}}; }
export function overwrite(source) { return {a: 1, ...source, a: 9}; }
export function empty(source) { return {...source}; }
export function discarded(source) { ({...source}); return 4; }
