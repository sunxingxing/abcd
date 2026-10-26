import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';
// These cases have explicit numeric/tag proof. Tagged INT32_MIN and dangerous
// NaN encodings are separately refused by decoded-input tests.
const values = [0, 7, -7, 2147483647, -2147483648, -0, Infinity, -Infinity];
for (const [index, value] of values.entries()) {
  const source = execFileSync(process.argv[2], ['--source', String(index)], {encoding: 'utf8'});
  assert.ok(!source.includes('INCOMPLETE'));
  const fn = runInNewContext(source);
  assert.ok(Object.is(fn(), -value));
}
console.log(`Proved legacy negation: ${values.length} native result/signed-zero/boundary scenarios passed`);

// These previously partial fixtures selected a retired pinned source contract.
// Their raw pinned proofs remain negative C++ checks; V10 emits a complete
// coercion. Compare every observable effect/throw of the original operations.
let upgraded = 0;
for (const [binary, selector, receiver] of [
  [process.argv[3], '--emit-negation', false],
  [process.argv[4], '--emit-source', true],
]) {
  const source = execFileSync(binary, [selector], {encoding: 'utf8'});
  assert.ok(!source.includes('INCOMPLETE'));
  const actual = runInNewContext('"use strict";(' + source + ')');
  const scenarios = [
    () => undefined, () => null, () => false, () => 0, () => -0,
    () => 2147483648, () => Infinity, () => NaN, () => 3n, () => Symbol('input'),
    log => ({valueOf() { log.push('valueOf'); return 7; }}),
    log => ({[Symbol.toPrimitive](hint) { log.push(hint); return 9n; }}),
    log => ({valueOf() { log.push('valueOf'); throw new RangeError('user'); }}),
  ];
  for (const make of scenarios) {
    const outcome = fn => {
      const log = [];
      const value = make(log);
      try { return {kind: 'return', value: fn(value), log}; }
      catch (error) {
        return {kind: error.constructor.name, log,
          userMessage: error.constructor === RangeError ? error.message : undefined};
      }
    };
    const original = value => { -value; };
    assert.deepEqual(outcome(value => receiver ? actual.call(value) : actual(value)), outcome(original));
    ++upgraded;
  }
}
console.log(`Previously partial V10 projections: ${upgraded} coercion/log/throw scenarios passed`);
