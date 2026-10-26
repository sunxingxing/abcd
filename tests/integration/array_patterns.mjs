// Differential: native array destructuring keeps the iterator protocol order:
// one cached next per pattern, a step per element (holes included), return()
// only when not done, no return() after a throwing step, and rethrow on errors.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
// A logged iterable: `values` are produced in order; `fail` makes that step throw.
const logged = (log, values, {fail = -1, closeThrows = false} = {}) => ({
  [Symbol.iterator]() {
    log.push('iterator'); let i = 0;
    return {
      get next() { log.push('get next'); return () => {
        log.push('next ' + i);
        if (i === fail) throw new Error('step ' + i);
        return i < values.length ? {done: false, value: values[i++]} : {done: true, value: 'end'};
      }; },
      return() { log.push('return'); if (closeThrows) throw new Error('close'); return {}; },
    };
  },
});
let cases = 0;
const outcome = async f => { try { return ['ok', await f()]; } catch (e) { return ['throw', e.constructor.name, e instanceof TypeError ? '' : e.message]; } };
const check = async observe => {
  const run = async m => { const log = []; const r = await outcome(() => observe(m, log)); return [r, log]; };
  assert.deepEqual(await run(b), await run(a)); cases++;
};
for (const values of [[], [1], [1, 2], [1, 2, 3]]) {
  await check((m, log) => m.pair(logged(log, values)));
  await check((m, log) => m.holes(logged(log, values)));
  await check((m, log) => m.trailing(logged(log, values)));
  await check((m, log) => m.reassign(logged(log, values), logged(log, values.slice(1))));
  await check((m, log) => m.swap(logged(log, values)));
  await check((m, log) => m.guarded(logged(log, values)));
}
for (const fail of [0, 1, 2]) {
  await check((m, log) => m.pair(logged(log, [1, 2, 3], {fail})));
  await check((m, log) => m.guarded(logged(log, [4, 5, 6], {fail})));
}
await check((m, log) => m.pair(logged(log, [1, 2, 3], {closeThrows: true})));
await check((m, log) => m.guarded(logged(log, [1, 2, 3], {closeThrows: true})));
for (const bad of [null, 5, {}, {[Symbol.iterator]: () => 1}])
  await check(m => m.guarded(bad));
await check(m => [m.pair('ab'), m.holes(new Set([7, 8, 9])), m.pair(new Map([[1, 2]]))]);
await check((m, log) => {
  let n = 0;
  return m.looped({more: () => n < 4, next: () => { n++; return logged(log, [n % 2, n * 10, 'extra']); }});
});
await check((m, log) => m.awaited(Promise.resolve(logged(log, [3, 4, 5]))));
console.log(JSON.stringify({cases}));
