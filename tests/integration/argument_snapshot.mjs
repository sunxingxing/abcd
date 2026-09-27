import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const cases = process.argv.slice(2).flatMap(executable =>
  execFileSync(executable, ['--snapshot-json'], {encoding: 'utf8'})
    .trim().split('\n').map(line => JSON.parse(line)));
assert.equal(cases.length, 51);

// Test-only embedding capability for the pinned runtime contract. Retained
// intrinsics avoid public prototype lookups; no input carrier escapes.
const apply = Reflect.apply;
const define = Object.defineProperty;
const descriptor = Object.getOwnPropertyDescriptor;
const strictArguments = function () { return arguments; };
const poison = descriptor(strictArguments(), 'callee');
// Zero formals means an empty parameter map. Its configurable initial callee
// lets the fixture install caller before callee, matching v7 ObjectFactory's
// layout and observable key order; strict native arguments cannot be reordered.
const makeArguments = Function('return arguments');
const iterator = Array.prototype.values;
const iteratorKey = Symbol.iterator;
const objectPrototype = Object.prototype;
let factoryCalls = 0;
const factory = (carrier, slots) => {
  ++factoryCalls;
  const values = [];
  for (let i = 0; i < carrier.length; ++i) {
    define(values, i, {value: i < slots.length ? slots[i] : carrier[i],
      enumerable: true, writable: true, configurable: true});
  }
  const result = apply(makeArguments, undefined, values);
  delete result.callee;
  define(result, 'caller', poison);
  define(result, 'callee', poison);
  return result;
};

function checkObject(actual, expected) {
  assert.equal(Object.getPrototypeOf(actual), objectPrototype);
  assert.equal(Object.prototype.toString.call(actual), '[object Arguments]');
  assert.equal(Array.isArray(actual), false);
  assert.deepEqual(descriptor(actual, 'length'), {
    value: expected.length, writable: true, enumerable: false, configurable: true,
  });
  assert.deepEqual(descriptor(actual, iteratorKey), {
    value: iterator, writable: true, enumerable: false, configurable: true,
  });
  for (const name of ['caller', 'callee']) {
    assert.deepEqual(descriptor(actual, name), poison);
    assert.throws(() => actual[name], TypeError);
    assert.throws(() => { actual[name] = null; }, TypeError);
  }
  for (let i = 0; i < expected.length; ++i) {
    assert.equal(actual[i], expected[i]);
    assert.deepEqual(descriptor(actual, i), {
      value: expected[i], writable: true, enumerable: true, configurable: true,
    });
  }
  assert.equal(Object.hasOwn(actual, expected.length), false);
  assert.deepEqual(Reflect.ownKeys(actual), [
    ...expected.map((_, i) => String(i)), 'length', 'caller', 'callee', iteratorKey,
  ]);
}

function checkLoop(actual, values) {
  const expected = values.slice();
  if (values.length) expected[0] = false;
  if (values.length > 1) {
    const second = actual[1];
    const first = second[1];
    const initial = values.slice(); initial[0] = true;
    checkObject(first, initial);
    const next = values.slice(); next[0] = false; next[1] = first;
    checkObject(second, next);
    assert.notEqual(first, second);
    assert.notEqual(first, actual);
    assert.notEqual(second, actual);
    expected[1] = second;
  }
  checkObject(actual, expected);
}

for (const item of cases) {
  assert.equal(item.positions.source_contract, 'language_and_host_v3');
  assert.equal(item.positions.argument_snapshot_binding.contract, 'language_and_host_v3');
  assert.equal(typeof item.source, 'string');
  assert.equal(typeof item.writes_parameter, 'boolean');
  assert.equal(typeof item.nested_snapshot, 'boolean');
  assert.equal(typeof item.branch_parameter, 'boolean');
  assert.equal(typeof item.loop_snapshots, 'boolean');
  const expression = item.method ? `(class { ${item.source} }).prototype.run` : `(${item.source})`;
  const fn = Function('factory', `"use strict"; const snapshotArguments = factory; return ${expression};`)(factory);
  assert.equal(fn.length, item.default_parameter ? 0 : item.parameters);
  assert.equal(fn.name, item.method ? 'run' : 'child');
  assert.equal(Object.hasOwn(fn, 'prototype'), !item.method && !item.arrow);
  assert.equal(item.positions.invocation_form, item.arrow ? 'arrow_method' : 'declared');
  if (item.arrow) {
    assert.equal(item.positions.callable_text_policy, 'recovered');
    assert.equal(Object.getPrototypeOf(fn), Function.prototype);
    assert.deepEqual(Reflect.ownKeys(fn), ['length', 'name']);
    for (const name of ['caller', 'arguments']) assert.throws(() => fn[name], TypeError);
    const bound = fn.bind({ignored: true});
    assert.equal(bound.length, fn.length);
    assert.equal(bound.name, `bound ${fn.name}`);
    assert.throws(() => Reflect.construct(bound, []), TypeError);
  }
  const counts = new Set([0, 1, Math.max(0, item.parameters - 1), item.parameters, item.parameters + 2]);
  for (const count of counts) {
    const values = Array.from({length: count}, (_, i) => i === 0 ? undefined : {ordinal: i});
    const before = factoryCalls;
    const result = apply(fn, {unrelated: true}, values);
    assert.equal(factoryCalls - before, item.loop_snapshots ? 3 : item.nested_snapshot ? 2 : 1);
    const second = apply(fn, null, values);
    const expected = values.slice();
    if (count && item.self_parameter) expected[0] = fn;
    if (count && item.default_parameter) expected[0] = -1;
    if (count && (item.writes_parameter || item.branch_parameter)) expected[0] = 37;
    if (count && item.nested_snapshot) {
      checkObject(result[0], values);
      checkObject(second[0], values);
      assert.notEqual(result[0], result);
      assert.notEqual(result[0], second[0]);
      expected[0] = result[0];
    }
    if (item.loop_snapshots) {
      checkLoop(result, values);
      checkLoop(second, values);
      if (count > 1) assert.notEqual(result[1], second[1]);
    } else checkObject(result, expected);
    assert.notEqual(result, second);
    if (count) {
      result[0] = 'changed';
      assert.notEqual(second[0], 'changed');
      assert.equal(values[0], undefined);
    }
    if (item.method || item.arrow) assert.throws(() => Reflect.construct(fn, values), TypeError);
    else {
      const constructed = Reflect.construct(fn, values);
      assert.equal(Object.prototype.toString.call(constructed), '[object Arguments]');
      if (item.loop_snapshots) checkLoop(constructed, values);
    }
    if (count && item.default_parameter) {
      for (const value of [null, false, 0, '', {input: true}]) {
        const supplied = values.slice(); supplied[0] = value;
        checkObject(apply(fn, undefined, supplied), supplied);
      }
    }
    if (count && item.branch_parameter) {
      for (const predicate of [false, 0, -0, true, 1, undefined, null, 0n]) {
        const branchValues = values.slice(); branchValues[0] = predicate;
        const branchExpected = branchValues.slice();
        branchExpected[0] = predicate === false || predicate === 0 ? 42 : 37;
        checkObject(apply(fn, undefined, branchValues), branchExpected);
      }
    }
  }
  const savedValues = descriptor(Array.prototype, 'values');
  const savedIterator = descriptor(Array.prototype, iteratorKey);
  let afterMutation;
  try {
    define(Array.prototype, 'values', {value() { throw Error('public values'); }, configurable: true});
    define(Array.prototype, iteratorKey, {value() { throw Error('public iterator'); }, configurable: true});
    afterMutation = apply(fn, undefined, [undefined, 2, 3]);
  } finally {
    define(Array.prototype, 'values', savedValues);
    define(Array.prototype, iteratorKey, savedIterator);
  }
  assert.equal(afterMutation[iteratorKey], iterator);
  const binding = item.positions.argument_snapshot_binding;
  assert.equal(binding.name, 'snapshotArguments');
  assert.equal(binding.policy, 'explicit_host');
  assert.equal(binding.abi, 'v7_current_slots');
  assert.equal(item.snapshots.length, item.nested_snapshot || item.loop_snapshots ? 2 : 1);
  for (const site of item.snapshots) {
    const source = item.positions.instruction_sources[site.instruction];
    assert.equal(item.positions.origins[source.origin].pc, site.pc);
    const range = source.range;
    assert.ok(range.begin < range.end && range.end <= Buffer.byteLength(item.source));
    assert.match(Buffer.from(item.source).subarray(range.begin, range.end).toString(), /snapshotArguments/);
  }
}
