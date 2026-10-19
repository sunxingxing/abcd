import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const sources = process.argv.slice(2).map(p => fs.readFileSync(p, 'utf8'));
for (const value of [false, true, 0, -0, NaN, 1, null, undefined, '', 'x', 0n, 1n,
  {valueOf() { throw new Error('ToBoolean must not call valueOf'); }}]) {
  for (const failure of ['none', 'input', 'observed']) {
    const results = sources.map(source => {
      const log=[];
      const context = vm.createContext({});
      for (const key of ['input', 'observed']) Object.defineProperty(context,key,{
        get() { log.push(key); if (failure===key) throw new TypeError(key); return value; }
      });
      try { vm.runInContext('"use strict";\n' + source,context); log.push('done'); }
      catch (e) { log.push(e.constructor.name); }
      return log;
    });
    assert.deepEqual(results[0],results[2]);
    assert.deepEqual(results[1],results[2]);
  }
}
