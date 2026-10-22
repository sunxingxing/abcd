export function make(proto) { return {a: 1, __proto__: proto, b: 2}; }
export function ordered(first, proto, last) { return {a: first(), __proto__: proto(), b: last()}; }
export function nested(proto) { return {inner: {__proto__: proto}, __proto__: null}; }
export function fresh() { return {__proto__: {inherited: 9}, a: 1}; }
