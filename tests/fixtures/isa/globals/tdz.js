globalThis.lexical = 9;
try { globalThis.observation = typeof lexical; }
catch (e) { globalThis.observation = e.name; }
let lexical = 1;
