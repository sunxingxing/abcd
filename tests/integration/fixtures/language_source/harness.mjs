const [file] = process.argv.slice(2);
globalThis.SCALE = 3;
globalThis.OFFSET = 10;
let m, error = null;
try { m = await import(file); } catch (e) { error = e.constructor.name; }
console.log(JSON.stringify({error, exports: m ? {result: m.result, shifted: m.shifted ? m.shifted(5) : null} : null}));
