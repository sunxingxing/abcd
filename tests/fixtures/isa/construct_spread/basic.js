export function run(xs) { return new (function C(...args) { this.args = args; })(...xs); }
export function mixed(xs) { return new (class C { constructor(...args) { this.args = args; } })(1, ...xs, 2); }
export function throws(xs) { return new (function C() { throw 17; })(...xs); }
