"use strict";
const fixed = 3;
const constWrite = function constWrite(value) { fixed = value; };
const missingWrite = function missingWrite(value) { missingGlobalForScript = value; };
const missingRead = function missingRead() { return missingReadForScript; };
globalThis.scriptObservation = fixed;
