import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for async completions through a copied promise
// register: resolution, inner catch, and rejection from outside the try.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const settle = p => Promise.resolve().then(p).then(v => ['ok', v], e => ['error', e.constructor.name]);
const observe = async path => {
  const m = await load(path);
  const log = [];
  const box = () => ({set: (k, v) => log.push(k + '=' + typeof v + ':' + v), get: () => log.length});
  const results = [];
  for (const run of [() => m.store(box(), async () => 1), () => m.store(box(), async () => { throw new Error('x'); }),
      () => m.store(null, async () => 2), () => m.load({cached: 5}), () => m.load({load: async () => 7}),
      () => m.load({load: async () => { throw new Error('y'); }}), () => m.load(null),
      () => m.nested(box(), async () => 3), () => m.nested(box(), async () => { throw new Error('z'); })])
    results.push(await settle(run));
  return {results, log};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 9}));
