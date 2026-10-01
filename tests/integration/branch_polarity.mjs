import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import vm from 'node:vm';
const source = execFileSync(process.argv[2], ['--polarity-sources'], { encoding: 'utf8' });
assert.doesNotMatch(source, /\belse\b/);
const functions = vm.runInNewContext(source);
assert.equal(functions.length, 4);
const references = [
  value => { const x = value.probe; if (!(x === false || x === 0)) value.items; },
  value => { const x = value.probe; if (!(x === true || (typeof x === 'number' && x !== 0))) value.items; },
  value => { if (value.probe !== undefined) value.items; },
  value => { if (!(value.probe !== undefined)) value.items; },
];
const sentinel = {};
const noCoercion = { [Symbol.toPrimitive]() { throw new Error('unexpected coercion'); } };
const values = [undefined, null, false, true, 0, -0, 1, -1, NaN, Infinity, '', 'text', 0n, 1n, Symbol('x'), noCoercion];
function run(fn, value, fail, replace) {
  const events = [];
  const receiver = {};
  const getter = function() {
    assert.equal(this, receiver); events.push('items');
    if (fail === 'items') throw sentinel;
    return noCoercion;
  };
  Object.defineProperty(receiver, 'items', { configurable: true, get: getter });
  Object.defineProperty(receiver, 'probe', { get() {
    assert.equal(this, receiver); events.push('probe');
    if (fail === 'probe') throw sentinel;
    if (replace) Object.defineProperty(receiver, 'items', { get() {
      events.push('replacement'); return getter.call(this);
    }});
    return value;
  }});
  try { assert.equal(fn(receiver), undefined); return { events }; }
  catch (error) { assert.equal(error, sentinel); return { events, error }; }
}
let count = 0;
for (let i = 0; i < functions.length; ++i) for (const value of values)
  for (const fail of ['', 'probe', 'items']) for (const replace of [false, true]) {
    assert.deepEqual(run(functions[i], value, fail, replace), run(references[i], value, fail, replace));
    ++count;
  }
console.log(`Empty taken arm orientation: ${count} predicate/getter scenarios passed`);
