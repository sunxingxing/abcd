import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for derived classes created by descendant or phased
// creators: instance shape, prototype chain, new.target, fields, methods and
// a throwing base constructor are compared between original and emitted.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const shape = o => ({own: Object.keys(o).map(k => [k, o[k]]), chain: (() => { const names = []; for (let p = Object.getPrototypeOf(o); p; p = Object.getPrototypeOf(p)) names.push(p.constructor.name); return names; })(), m: typeof o.m === 'function' ? o.m() : null});
const run = f => { try { return shape(f()); } catch (e) { return ['threw', e.message]; } };
let cases = 0;
const observe = async path => {
  globalThis.__probe = () => { throw new Error('probe'); };
  const module = await load(path);
  const results = [];
  for (const x of [1, 'boom', 0]) {
    results.push(run(() => new module.D(x)), run(() => module.make(x)), run(() => module.nested(x)));
  }
  return {results, log: String(globalThis.__log)};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original); cases = original.results.length;
console.log(JSON.stringify({cases}));
