import assert from 'node:assert/strict';
import {runInNewContext} from 'node:vm';
import {generatedSource} from '../support/source/generated_fixture.mjs';
const rows = generatedSource(process.argv[2], ['--emit'], {encoding:'utf8', maxBuffer:16000000, timeout:120000}).trim().split('\n').map(JSON.parse);
const token = {}, failure = {}, symbol = Symbol('value');
const hostile = {[Symbol.toPrimitive]() { assert.fail('data property value coercion'); }};
const values = [undefined, null, false, -0, NaN, 7n, symbol, token, hostile];
let comparisons = 0, mutants = 0;
for (const row of rows) {
  assert(!row.source.includes('INCOMPLETE'));
  const fn = runInNewContext(row.source);
  const execute = (fn, value, failAt) => {
    const events = [];
    const output = fn(function(index) {
      assert.equal(this, undefined);
      events.push(index);
      if (index === failAt) throw failure;
      return value;
    });
    const expectedEvents = Array.from({length: failAt < 0 ? row.count : failAt + 1}, (_, i) => i);
    assert.deepEqual(events, expectedEvents);
    if (failAt >= 0) assert.equal(output, failAt + 1);
    else {
      assert.equal(Array.isArray(output), row.array);
      const expectedKeys = Array.from({length:row.count}, (_, i) => row.array ? String(i) : i === 0 ? '__proto__' : `field${i}`);
      assert.deepEqual(Object.keys(output), expectedKeys);
      for (const key of expectedKeys) {
        const property = Object.getOwnPropertyDescriptor(output, key);
        assert.equal(property.writable, true); assert.equal(property.configurable, true); assert.equal(property.enumerable, true);
        assert(Object.is(property.value, value));
      }
      if (!row.array) assert(Object.hasOwn(output, '__proto__'));
    }
    return output;
  };
  for (const value of values) for (const failAt of new Set([-1, 0, Math.floor(row.count / 2), row.count - 1])) {
    const first = execute(fn, value, failAt);
    if (failAt < 0) assert.notEqual(first, execute(fn, value, failAt));
    ++comparisons;
  }
  // An inherited setter must not run for native own data definitions.
  const context = {};
  const protectedFn = runInNewContext(`Object.defineProperty(Object.prototype, 'field1', {set(){throw 'setter';}, configurable:true});\n${row.source}`, context);
  execute(protectedFn, token, -1);
  // Mutating the independent callback order must fail the same acceptance.
  const reordered = callback => fn(index => callback(row.count - 1 - index));
  if (row.count > 1) {assert.throws(() => execute(reordered, token, 0)); ++mutants;}
  const lostSnapshot = callback => {const value = fn(callback); return typeof value === 'number' ? 0 : value;};
  assert.throws(() => execute(lostSnapshot, token, 0)); ++mutants;
}
assert.equal(rows.length, 28);
console.log(`${rows.length} aggregate sources / ${comparisons} independent execution cases / ${mutants} adverse mutants`);
