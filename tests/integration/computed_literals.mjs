import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for object literals with computed keys defined by
// DEFINEFIELDBYVALUE / DEFINEFIELDBYINDEX. Keys of every kind, including an
// object whose toString logs, compare key conversion count/order and results.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const symbol = Symbol('s');
const describe = o => Reflect.ownKeys(o).map(k => [typeof k === 'symbol' ? 'symbol' : k, o[k],
    Object.getOwnPropertyDescriptor(o, k).enumerable]);
let cases = 0;
const observe = async path => {
  const module = await load(path);
  const log = [];
  const keys = () => ['k', 3, 0, symbol, {toString() { log.push('toString'); return 'obj'; }},
                      {toString() { log.push('throw'); throw new Error('key'); }}];
  const results = [];
  for (const name of ['only', 'mixed', 'indexed', 'repeated'])
    for (const key of keys()) {
      try { results.push([name, describe(module[name](key, 'x'))]); }
      catch (error) { results.push([name, 'threw', error.message]); }
    }
  return {results, log};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original); cases = original.results.length;
console.log(JSON.stringify({cases}));
