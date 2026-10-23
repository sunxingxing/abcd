// Differential for synchronous generators: next/throw/return plans, sent
// values, and yield* delegation to traced custom iterators (forwarding of
// next/throw/return, delegated completion value).
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original, emitted] = process.argv.slice(2);
const load = p => import('data:text/javascript;base64,' + Buffer.from(readFileSync(p)).toString('base64'));
const a = await load(original), b = await load(emitted);
const drive = (it, plan) => {
  const out = [];
  for (const [k, v] of plan) {
    try { const r = it[k](v); out.push([r.value, r.done]); }
    catch (e) { out.push(['throw', e instanceof Error ? e.message : e]); }
  }
  return out;
};
const plans = [
  [['next'], ['next'], ['next'], ['next']],
  [['next'], ['throw', 7], ['next']],
  [['next'], ['return', 9], ['next']],
  [['throw', 1], ['next']],
  [['return', 2], ['next']],
  [['next', 1], ['next', 2], ['next', 3], ['next', 4], ['next', 5]],
];
const inner = (trace, mode) => ({
  [Symbol.iterator]() { trace.push('iter'); return this; },
  n: 0,
  next(v) { trace.push('next:' + v); this.n += 1; return {value: this.n * 10, done: this.n > 2}; },
  ...(mode !== 'no-return' && {return(v) { trace.push('return:' + v); return {value: 'r', done: true}; }}),
  ...(mode !== 'no-throw' && {throw(v) { trace.push('throw:' + v); return {value: 't', done: true}; }}),
});
let cases = 0;
for (const [name, args] of [['simple', []], ['echo', [0]], ['echo', [3]]])
  for (const plan of plans) { assert.deepEqual(drive(b[name](...args), plan), drive(a[name](...args), plan), name); cases++; }
for (const mode of ['full', 'no-return', 'no-throw'])
  for (const plan of plans) {
    const ta = [], tb = [];
    const ra = drive(a.delegate(inner(ta, mode)), plan), rb = drive(b.delegate(inner(tb, mode)), plan);
    assert.deepEqual(rb, ra, 'delegate ' + mode); assert.deepEqual(tb, ta, 'delegate trace ' + mode); cases++;
  }
for (const plan of plans) { assert.deepEqual(drive(b.delegate([4, 5, 6]), plan), drive(a.delegate([4, 5, 6]), plan)); cases++; }
console.log(JSON.stringify({cases}));
