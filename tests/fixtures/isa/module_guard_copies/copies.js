export let X = class { constructor(a, b) { this.a = a; this.b = b; } };
export let helper = (u, v) => [u, v];
export const make = function () { return new X(undefined, {k: 1}); };
export const call = function (y) { return helper(y, X.name); };
export const pass = function () { return helper(X, helper); };
