import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for children reading module bindings initialized
// before their creation (vars, hoisted functions), including calls during
// module evaluation before a later assignment.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  globalThis.__probe = () => { throw new Error('probe'); };
  const module = await load(path);
  return {log: globalThis.__log, m: new module.K().m(), s: module.K.s(), read: module.read(), early: module.early()};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 5}));
