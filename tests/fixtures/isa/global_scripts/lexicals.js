"use strict";
let mutable = 2;
const fixed = 3;
const twice = function twice(x) { return x * 2; };
const bump = function bump(x) { mutable = mutable + x; return mutable; };
class GlobalClass { value() { return fixed; } }
mutable = mutable + 1;
globalThis.scriptObservation = [twice(3), mutable, fixed, bump(4), new GlobalClass().value()];
