"use strict";
const fixed = 3;
function constWrite(value) { fixed = value; }
function missingWrite(value) { missingGlobalForScript = value; }
function missingRead() { return missingReadForScript; }
globalThis.scriptObservation = fixed;
