import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for hoisted functions assigning module bindings behind
// an explicit Hole guard: an early call hits the TDZ, later calls assign.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  const module = await load(path);
  const log = globalThis.__log.slice();
  const after = [module.setX(9), module.x, module.bump(3), module.bump(-1)];
  return {log, after, x: module.x};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: original.log.length + original.after.length}));
