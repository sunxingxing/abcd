import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle: static initializer values and order, accessors,
// instance fields and arrows that read the captured class binding.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const describe = (o, k) => { const d = Object.getOwnPropertyDescriptor(o, k); return d ? [typeof d.get, typeof d.value, d.enumerable, d.writable] : null; };
const observe = async path => {
  const m = await load(path);
  const p = new m.Plain(), f = new m.Fields(), x = new m.Mixed();
  return {plain: [m.Plain.s, m.Plain.t, p.m, p.run()(), describe(m.Plain.prototype, 'm'), describe(m.Plain, 't')],
          fields: [f.x, f.run()(), m.Fields.s, describe(f, 'x')],
          mixed: [x.y, x.z, x.later()(), x.later()(), m.Mixed.count, describe(m.Mixed.prototype, 'z')]};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 3}));
