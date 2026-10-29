import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Differential oracle for phased-route selections: getter order, receiver
// binding, single evaluation of the tested value and selected arm.
const [originalPath, emittedPath] = process.argv.slice(2);
let fresh = 0;
const load = path => import('data:text/javascript;base64,' +
    Buffer.from(`${readFileSync(path, 'utf8')}\n// ${++fresh}`).toString('base64'));
const settle = p => Promise.resolve().then(p).then(v => ['ok', v], e => ['error', e.constructor.name]);
const observe = async path => {
  const m = await load(path);
  const calls = [];
  const counted = v => () => { calls.push(v); return v; };
  const results = [];
  for (const run of [() => m.pick({p: 1}), () => m.pick({p: 0}), () => m.fallback(null), () => m.fallback(0),
      () => m.either(counted(0), 'y'), () => m.either(counted('z'), 'y'),
      () => m.guarded(() => { throw new Error('t'); }, 0, 'u'), () => m.guarded(() => 1, 'v', 'u'),
      () => m.plain(undefined), () => m.plain(false), () => m.joined('', 3), () => m.joined('a', 3),
      () => m.passed(async v => calls.push(v), null), () => m.passed(async v => calls.push(v), 'q')])
    results.push(await settle(run));
  return {results, calls, log: m.log.slice()};
};
const [original, emitted] = [await observe(originalPath), await observe(emittedPath)];
assert.deepEqual(emitted, original);
console.log(JSON.stringify({cases: 14}));
