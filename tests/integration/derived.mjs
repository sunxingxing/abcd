// Differential for derived-class construction: implicit forwarding constructors,
// explicit super(...spread), built-in heritage and empty bases.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const observe = (m, name, args, viaReflect) => {
  const C = m[name];
  try {
    const v = viaReflect ? Reflect.construct(C, args, class extends C {}) : new C(...args);
    return [Object.getPrototypeOf(Object.getPrototypeOf(v)) === (viaReflect ? C.prototype : Object.getPrototypeOf(C.prototype)),
      v instanceof C, Array.isArray(v), v.length, v.xs, v.n, Object.keys(v), C.length, C.name,
      Object.getPrototypeOf(C) === (name === 'List' ? Array : name === 'FromPlain' ? m.Plain : name === 'Base' || name === 'Plain' ? Function.prototype : m.Base)];
  } catch (e) { return ['throw', e.name]; }
};
let cases = 0;
for (const name of ['Base', 'Implicit', 'Spread', 'List', 'Plain', 'FromPlain'])
  for (const args of [[], [1], [1, 2, 3], [5], ['x', null]])
    for (const viaReflect of [false, true]) { assert.deepEqual(observe(b, name, args, viaReflect), observe(a, name, args, viaReflect), name); cases++; }
for (const name of ['Implicit', 'Spread', 'List']) { const call = m => { try { m[name](); return 'returned'; } catch (e) { return e.name; } }; assert.equal(call(b), call(a)); cases++; }
console.log(JSON.stringify({cases}));
