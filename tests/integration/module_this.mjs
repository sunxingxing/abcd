import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for module-level `this` captured by arrows: module code
// is strict and runs with `this` undefined, so every capture observes it.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  const seen = [];
  globalThis.__probe = value => { seen.push(typeof value); throw new Error('probe'); };
  const module = await load(path);
  return {direct: module.direct(), nested: module.nested(), method: module.method.m(),
          captured: module.captured(4), seen, log: globalThis.__log};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 5}));
