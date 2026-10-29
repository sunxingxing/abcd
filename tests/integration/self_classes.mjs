import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for classes whose methods or static members refer to
// their own binding, created by functions and by phased module entries.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  globalThis.__probe = () => { throw new Error('probe'); };
  const module = await load(path);
  const run = f => { try { return f(); } catch (e) { return ['threw', e.constructor.name]; } };
  return {mk: run(() => module.mk()), statics: run(() => module.statics(5)), derived: run(() => module.derived(7)),
          early: run(() => module.early()), log: globalThis.__log};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 4}));
