import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

// Independent handwritten references. This isolates AST printing; it does not
// certify VM numeric domains or replace decoded-input operator tests.
const references = [
  (a,b,c) => a - (b - c), (a,b,c) => (a - b) - c,
  (a,b,c) => a / (b / c), (a,b,c) => (a / b) / c,
  (a,b,c) => (a ** b) ** c, (a,b,c) => a ** (b ** c),
  (a,b) => (-a) ** b, (a,b) => -(a ** b),
  (a,b,c) => a + b * c, (a,b,c) => (a + b) * c,
  (a,b,c) => a + (b + c), a => -(-a), (a,b) => ~(a & b),
  (a,b) => !(a instanceof b), (a,b,c) => (a < b) == c,
  (a,b,c) => a < (b == c), (a,b) => a ** -b, (a,b) => typeof (a + b),
  (a,b,c) => a & (b | c), (a,b,c) => (a & b) | c,
  (a,b,c) => a ?? (b && c), (a,b,c) => (a ?? b) && c,
  (a,b,c) => a || (b && c), (a,b,c) => (a || b) ?? c,
  (a,b,c) => a && (b || c), (a,b,c) => a ?? (b ?? c),
  (a,b,c) => (a ?? b) ?? c, (a,b,c) => (a && b) || c,
  async (a,b,c) => await (a + b * c), (a,b,c) => (0, a || b)(c)
];
const seeds = [[7,3,2], [-0,2,1], [NaN,2,3], [Infinity,-Infinity,2],
  ['x',2,'z'], [2n,3n,2n], [2n,3,2], [Symbol('value'),2,3],
  [null,false,3], [undefined,true,0], [false,null,2], [0,undefined,2],
  [null,function(value) { return [this, value]; },7], [function(value) { return [this, value]; },null,8]];
async function observe(fn, seed, wrap, mutate, failAt) {
  const events = [], failure = {}, state = [...seed]; let steps = 0;
  const args = seed.map((value, index) => wrap ? {
    [Symbol.toPrimitive](hint) {
      events.push([index, hint]);
      if (++steps === failAt) throw failure;
      if (mutate && index === 0) state[1] = 'changed';
      return state[index];
    }
  } : value);
  try { let value = fn(...args);
    if (value && typeof value.then === 'function') { events.push('invoked'); value = await value; }
    events.push(['return', args.includes(value) && typeof value === 'object' && value !== null ? ['argument', args.indexOf(value)] : value]); }
  catch (error) {
    if (error === failure) events.push('injected');
    else { assert.ok(error.name === 'TypeError' || error.name === 'RangeError'); events.push(error.name); }
  }
  return events;
}
let scenarios = 0;
for (const [index, reference] of references.entries()) {
  const source = execFileSync(process.argv[2], ['--source', String(index)], {encoding: 'utf8'});
  const generated = runInNewContext(source);
  for (const seed of seeds) for (const wrap of [false, true]) for (const mutate of [false, true])
    for (let failAt = 0; failAt <= 4; ++failAt) {
      assert.deepEqual(await observe(generated, seed, wrap, mutate, failAt),
        await observe(reference, seed, wrap, mutate, failAt), `expression ${index}`);
      ++scenarios;
    }
}
console.log(`Expression precedence: ${scenarios} grouping/coercion/exception scenarios passed`);
