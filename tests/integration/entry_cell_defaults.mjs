import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for literal and cell-backed defaults that follow stores
// of the invocation entries (callee, new.target, this) into a fresh environment.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  const m = await load(path);
  const v = new m.View(1, {}), w = new m.View(undefined, {}, 0, 7);
  return {v: [v.show(), v.local, v.id], w: [w.show(), w.local, w.id], length: [m.View.length, m.Made.length, m.sum.length, m.self.length],
          made: [new m.Made(1).constructor === m.Made, m.Made(1), m.Made(1, undefined), m.Made(1, null)],
          sum: [m.sum(1), m.sum(1, 4), m.sum('a', undefined)], self: [m.self(1), m.self(1, 2)],
          many: [m.many.length, m.many(1), m.many(1, 0, null, 0), m.many(1, undefined, 5)]};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 7}));
