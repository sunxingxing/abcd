"use strict";
var before = twice(3);
var counter = 1;
let mutable = 2;
const fixed = 3;
function twice(x) { return x * 2; }
function bump(x) { mutable = mutable + x; return mutable; }
class GlobalClass { value() { return fixed; } }
counter = counter + 1;
mutable = mutable + 1;
globalThis.scriptObservation = [before, counter, mutable, fixed, bump(4), new GlobalClass().value()];
