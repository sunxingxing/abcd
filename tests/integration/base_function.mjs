import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const source = await import(pathToFileURL(process.argv[2]));
const values = [undefined, null, true, false, 0, -0, NaN, Infinity,
                'value', 42n, Symbol('value'), Object.freeze({value: 1})];
for (const value of values) {
  assert.ok(Object.is(source.parameter(value), value));
  assert.ok(Object.is(source.parameter(value, 9, 10), value));
  assert.ok(Object.is(source.receiver.call(value), value));
  assert.ok(Object.is(source.overwriteSelf(value), value));
  assert.ok(Object.is(source.lexical(value), value));
  assert.throws(() => source.thrown(value), error => Object.is(error, value));
  const constructed = new source.parameter(value);
  if (value !== null && (typeof value === 'object' || typeof value === 'function')) {
    assert.equal(constructed, value);
  } else {
    assert.equal(Object.getPrototypeOf(constructed), source.parameter.prototype);
  }
}
assert.equal(source.parameter(), undefined);
assert.equal(source.local('not a local'), undefined);
assert.equal(source.newTarget(), undefined);
assert.equal(source.self(), source.self);
assert.equal(source.intrinsicSelf(), source.intrinsicSelf);
assert.equal(source.intrinsicAfterSlotWrite(), source.intrinsicAfterSlotWrite);
assert.equal(source.independent(), source.independent);
assert.equal((0, source.receiver)(), undefined);
assert.equal(source.receiver(), source);
assert.equal(new source.newTarget(), source.newTarget);
function Other() {}
assert.equal(Reflect.construct(source.newTarget, [], Other), Other);
assert.equal(new source.self(), source.self);
const instance = new source.receiver();
assert.equal(Object.getPrototypeOf(instance), source.receiver.prototype);
assert.ok(instance instanceof source.receiver);
assert.equal(Object.getPrototypeOf(Reflect.construct(source.receiver, [], Other)), Other.prototype);
let prototypeReads = 0;
const alternatePrototype = Object.freeze({alternate: true});
const observedTarget = new Proxy(Other, {
  get(target, key, receiver) {
    if (key === 'prototype') {
      ++prototypeReads;
      return alternatePrototype;
    }
    throw new Error('unexpected constructor property read');
  }
});
assert.equal(Object.getPrototypeOf(Reflect.construct(source.receiver, [], observedTarget)), alternatePrototype);
assert.equal(prototypeReads, 1);
const getterError = Object.freeze({getterError: true});
const throwingTarget = new Proxy(Other, {
  get() { throw getterError; }
});
assert.throws(() => Reflect.construct(source.thrown, ['body must not run'], throwingTarget),
              error => error === getterError);
const first = source.fresh();
const second = source.fresh();
assert.notEqual(first, second);
assert.notEqual(first.prototype, second.prototype);
assert.equal(first(), first);
assert.equal(second(), second);
for (const fn of [source.parameter, source.self, first, second]) {
  assert.equal(fn.name, 'Recovered');
  assert.equal(fn.length, 1);
  assert.deepEqual(Object.getOwnPropertyDescriptor(fn, 'name'),
                   {value: 'Recovered', writable: false, enumerable: false, configurable: true});
  assert.deepEqual(Object.getOwnPropertyDescriptor(fn, 'length'),
                   {value: 1, writable: false, enumerable: false, configurable: true});
  assert.deepEqual(Object.getOwnPropertyDescriptor(fn, 'prototype'),
                   {value: fn.prototype, writable: true, enumerable: false, configurable: false});
  assert.equal(fn.prototype.constructor, fn);
  assert.throws(() => fn.caller, TypeError);
  assert.throws(() => fn.arguments, TypeError);
}
assert.equal(source.empty.length, 0);
assert.equal(source.length255.length, 255);
assert.equal(source.empty(), undefined);
