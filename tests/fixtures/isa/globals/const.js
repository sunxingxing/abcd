"use strict";
const fixed = 1;
try { fixed = 2; } catch (e) { globalThis.observation = e.name; }
