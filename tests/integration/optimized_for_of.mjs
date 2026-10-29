import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for optimized for...of: values, early exits,
// and iterator close on break/return/throw.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const attempt = f => { try { return ['ok', f()]; } catch (e) { return ['error', e.constructor.name]; } };
const observe = async path => {
  const m = await load(path);
  const seen = [], log = [];
  const iterable = items => ({[Symbol.iterator]() {
    let i = 0;
    return {next() { log.push('next'); return i < items.length ? {value: items[i++], done: false} : {value: undefined, done: true}; },
            return() { log.push('return'); return {done: true}; }};
  }});
  const results = [attempt(() => m.each([1, 2], x => seen.push(x))), attempt(() => m.total([1, 2, 3])),
      attempt(() => m.first([1, 2, 3])), attempt(() => m.first([])), attempt(() => m.until([1, 2, 3, 4])),
      attempt(() => m.skip([1, 2, 3])), 
      attempt(() => m.source(iterable([1, 2, 3]), x => x * 2)),
      attempt(() => m.source(iterable([1, 2]), x => { if (x === 2) throw new Error('boom'); return x; })),
      attempt(() => m.each(null, x => x)), attempt(() => m.first(iterable([1, 5, 9]))), attempt(() => m.until(iterable([1, 3, 4])))];
  return {results, seen, log};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 11}));
