import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for concurrent functions: results of every return
// path, including an async one, and the preserved directive.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  const m = await load(path);
  return {add: [m.add(1, 2), m.add('a', 'b')], pick: [m.pick(2), m.pick(0)], later: await m.later(5)};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
assert.ok(readFileSync(emittedPath, 'utf8').includes('"use concurrent"'));
console.log(JSON.stringify({cases: 5}));
