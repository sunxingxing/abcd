"use strict";
let current = 1;
const make = function make() { return () => current; };
const first = make();
current = 9;
globalThis.scriptObservation = [first(), make()(), first === first];
