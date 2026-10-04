import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Independent source-AST/printer oracle. Decoded input and pinned VM numeric
// admission remain separate; Boolean observation does not enlarge either.
const references = [
  a => !!a.value ? true : false,
  a => !!a.value,
  (a,b) => (!!a.value || !!b.value) ? true : false,
  (a,b) => !!a.value || !!b.value,
  (a,b) => (!!a.value && !!b.value) ? true : false,
  (a,b) => (!!a.value ?? !!b.value) ? true : false,
  a => !(!!a.value),
  (a,b) => !!(!!a.value || !!b.value) ? true : false,
  (a,b) => !!(a.value < b.value) ? true : false,
  a => ~!!a.value,
  (a,b,c) => (!!(a.value || b.value) && !!c.value) ? true : false,
  (a,b) => !(!!(a.value ?? b.value)),
  (a,b,c) => !!a.value ? !!b.value : !!c.value,
  (a,b,c) => ( !!a.value ? !!b.value : !!c.value ) ? true : false,
];
const sources = references.map((_, i) => execFileSync(process.argv[2],
  ['--boolean-context', String(i)], {encoding: 'utf8'}));
const generated = sources.map(text => vm.runInNewContext(text));
assert.match(sources[0], /return !!a\.value;/);
assert.match(sources[1], /return !!a\.value;/);
assert.match(sources[5], /!!a\.value \?\? !!b\.value/);
assert.match(sources[10], /\(a\.value \|\| b\.value\) && c\.value/);
assert.match(sources[11], /!\(a\.value \?\? b\.value\)/);

function observe(fn, left, right, mutate, failAt) {
  const trace = [], failure = Symbol('failure'); let steps = 0;
  const event = (...parts) => {trace.push(parts); if (++steps === failAt) throw failure;};
  const conversion = {[Symbol.toPrimitive](hint) {event('coerce', hint); return 7;}};
  const forbidden = {[Symbol.toPrimitive]() {assert.fail('Boolean conversion called user code');}};
  const values = [undefined, null, false, true, 0, -0, NaN, Infinity, '', 'text', 0n, 3n,
    Symbol('value'), conversion, forbidden, Object.create(null)];
  const state = [values[left], values[right], values[right]];
  const args = state.map((_, index) => {
    let receiver;
    const target = Object.defineProperty({}, 'value', {get() {
      assert.equal(this, receiver); event('getter', index);
      if (mutate && index === 0) state[1] = state[2] = undefined;
      return state[index];
    }});
    receiver = new Proxy(target, {get(object, key, owner) {
      assert.equal(key, 'value'); event('get', index); return Reflect.get(object, key, owner);
    }});
    return receiver;
  });
  try {return {trace, value: fn(...args)};}
  catch (error) {
    if (error === failure) return {trace, threw: 'injected'};
    // The comparison alone may coerce values. The deliberately forbidden
    // conversion is checked by its assertion, never treated as a VM failure.
    if (left === 14 || right === 14) throw error;
    assert.equal(error.name, 'TypeError'); return {trace, threw: 'TypeError'};
  }
}
let comparisons = 0;
for (const [i, reference] of references.entries()) for (let left = 0; left < 16; ++left)
  for (let right = 0; right < 16; ++right) for (const mutate of [false, true])
    for (let failAt = 0; failAt <= 9; ++failAt) {
      // Relational comparison legitimately invokes coercion hooks; replace
      // the hostile hook with the independent converting payload in that case.
      const a = i === 8 && left === 14 ? 13 : left;
      const b = i === 8 && right === 14 ? 13 : right;
      assert.deepEqual(observe(generated[i], a, b, mutate, failAt),
        observe(reference, a, b, mutate, failAt), `context ${i}`);
      ++comparisons;
    }
for (const observation of [0, 1]) for (const count of [1, 2, 200, 201, 402, 804]) {
  const source = execFileSync(process.argv[2], ['--boolean-chain', String(observation), String(count)], {encoding: 'utf8'});
  const actual = vm.runInNewContext(source);
  const reference = a => {let result = a.value; for (let i = 0; i < count; ++i) result = !result;
    return observation === 0 ? (result ? true : false) : result;};
  for (let value = 0; value < 16; ++value) for (const mutate of [false, true]) for (let failAt = 0; failAt <= 9; ++failAt) {
    assert.deepEqual(observe(actual, value, 0, mutate, failAt), observe(reference, value, 0, mutate, failAt));
    ++comparisons;
  }
}
console.log(`${comparisons} Boolean-context value/short-circuit/getter/exception comparisons passed`);
