import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for await continuations that consume the result
// register directly, including rejected promises.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const settle = promise => promise.then(value => ['ok', value], error => ['error', String(error)]);
const observe = async path => {
  const m = await load(path);
  const pair = (x, y) => `${x}|${y}`;
  const seen = [];
  return Promise.all([settle(m.accumulate(3)), settle(m.accumulate(Promise.resolve('s'))),
      settle(m.first(pair, Promise.resolve(1))), settle(m.first(pair, Promise.reject(new Error('r')))),
      settle(m.sum(Promise.resolve(2), 3)), settle(m.sum('a', 'b')),
      settle(m.tagged(pair, Promise.resolve('t'))), settle(m.ignored(x => { seen.push(x); }, Promise.resolve('i'))),
      settle(m.tested(Promise.resolve(0))), settle(m.tested(true)), settle(m.tested(Promise.reject(new Error('x')))),
      settle(m.incremented(x => x * 2, Promise.resolve(4)))]).then(results => [results, seen]);
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 12}));
