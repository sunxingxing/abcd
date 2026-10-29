import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for class accessors installed outside the literal
// buffer by phased creators (module entry or function with try/catch):
// descriptors, getter/setter behaviour and static accessors.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const describe = (o, k) => { const d = Object.getOwnPropertyDescriptor(o, k); return [typeof d.get, typeof d.set, d.enumerable, d.configurable]; };
const observe = async path => {
  globalThis.__probe = () => { throw new Error('probe'); };
  const module = await load(path);
  const a = new module.A(); const before = a.v; a.v = 5;
  module.A.s = 9;
  return {before, after: a.v, s: module.A.s, log: globalThis.__log, proto: describe(module.A.prototype, 'v'),
          stat: describe(module.A, 's'), mk: module.mk(1)};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 6}));
