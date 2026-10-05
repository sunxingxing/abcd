import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createContext, runInContext, runInNewContext} from 'node:vm';

const cases = [
  ['--source', '(function() { return {field0: Lookup.left + "suffix", field1: Lookup.left + "suffix"}; })'],
  ['--equal-source', '(function() { return {field0: Lookup.left === Lookup.left, field1: Lookup.left === Lookup.left}; })'],
  ['--less-source', '(function() { return {field0: Lookup.left < Lookup.left, field1: Lookup.left < Lookup.left}; })'],
  ['--bit-source', '(function() { return {field0: Lookup.left | Lookup.left, field1: Lookup.left | Lookup.left}; })']
];
const values = [undefined, null, false, true, -0, NaN, Infinity, 3, '4', 5n, Symbol('value')];
const failure = {};
function execute(source, reference, left, right, coercion, failAt) {
  const events = []; let steps = 0, reads = 0;
  const event = name => { events.push(name); if (++steps === failAt) throw failure; };
  const converted = value => coercion ? {
    [Symbol.toPrimitive](hint) { event(`primitive:${hint}`); return value; }
  } : value;
  const first = converted(left), second = converted(right);
  const object = {get left() { event('left'); return reads++ % 2 ? second : first; }};
  const lookup = () => { event('lookup'); return object; };
  const context = {};
  Object.defineProperty(context, 'Lookup', {get: lookup});
  const fn = runInNewContext(reference ?? source, context);
  try {
    const result = fn();
    assert.deepEqual(Object.keys(result), ['field0', 'field1']);
    for (const key of Object.keys(result)) {
      const descriptor = Object.getOwnPropertyDescriptor(result, key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
      events.push([key, descriptor.value]);
    }
    events.push('return');
  } catch (error) {
    if (error === failure) events.push('failure');
    else { assert.ok(error.name === 'TypeError' || error.name === 'ReferenceError'); events.push(error.name); }
  }
  return events;
}
let scenarios = 0;
for (const [option, reference] of cases) {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  assert.ok(!source.includes('INCOMPLETE'));
  for (const left of values) for (const right of values) for (const coercion of [false, true])
    for (let failAt = 0; failAt <= 12; ++failAt) {
      assert.deepEqual(execute(source, null, left, right, coercion, failAt),
        execute(null, reference, left, right, coercion, failAt));
      ++scenarios;
    }
}
console.log(`Native binary literal: ${scenarios} coercion/effect/exception scenarios passed`);

// Actual encoded bodies vary register copies, splitting, repeated pure
// decisions and reverse physical blocks. Only bytecode enters the engine.
const orderedSource = execFileSync(process.argv[2], ['--ordered-sources'], {
  encoding: 'utf8', maxBuffer: 4 * 1024 * 1024,
});
assert.doesNotMatch(orderedSource, /INCOMPLETE/);
assert.doesNotMatch(orderedSource, /\.field\d+\s*=/);
const orderedContext = createContext({});
const factories = runInContext(`'use strict'; ${orderedSource}`, orderedContext);
assert.equal(factories.length, 60);
const prototype = runInContext('Object.prototype', orderedContext), setterEvents = [];
for (let i = 0; i < 402; ++i) {
  const key = i ? `field${i}` : '__proto__';
  Object.defineProperty(prototype, key, {configurable: true,
    get() { throw new Error('unfinished literal property read'); },
    set(value) { setterEvents.push([key, value]); },
  });
}
const noCoercion = new Proxy({}, {get() { throw new Error('condition must not coerce'); }});
let orderedCases = 0;
for (let profile = 0, position = 0; profile < 3; ++profile)
  for (let layout = 0; layout < 4; ++layout) for (const count of [1, 2, 200, 201, 402]) {
    const factory = factories[position++];
    for (const condition of [...values, noCoercion]) {
      const result = factory(condition), other = factory(condition);
      assert.notEqual(result, other); assert.equal(Object.getPrototypeOf(result), prototype);
      assert.deepEqual(Reflect.ownKeys(result), Array.from({length: count}, (_, i) => i ? `field${i}` : '__proto__'));
      for (let i = 0; i < count; ++i) assert.deepEqual(Object.getOwnPropertyDescriptor(result, i ? `field${i}` : '__proto__'), {
        value: i, writable: true, enumerable: true, configurable: true,
      });
      assert.deepEqual(setterEvents, []); ++orderedCases;
    }
  }
console.log(`Ordered native literal: ${orderedCases} identity/property/control scenarios passed`);
