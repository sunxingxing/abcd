import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for conditional/logical member-call arguments: the
// member getter runs before the arguments, the call keeps its receiver, and
// only the selected arm is evaluated.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const observe = async path => {
  const m = await load(path);
  const probe = {get x() { m.log.push('x'); return 7; }};
  const source = value => ({get() { m.log.push('get'); return value; }});
  const results = [m.choose(0), m.choose('s'), m.either(0, 5), m.either('a', 5), m.both(null), m.both(probe),
      m.fallback(null), m.fallback(undefined), m.fallback(0), m.ordered(source(1), null), m.ordered(source(2), 4)];
  return {results, log: m.log.slice()};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 11}));
