import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {recovered: C, recoveredClosure: Closure,
       recoveredStaticClosure: StaticClosure} =
  await import(pathToFileURL(process.argv[2]));
const outer = new Closure();
const first = outer.method();
const second = outer.method();
assert.notEqual(first, second);
assert.equal(first.name, 'Inner');
assert.equal(first.length, 1);
assert.equal(first(17), 17);
assert.equal(second(23), 23);
const staticInner = StaticClosure.method();
assert.equal(staticInner(31), 31);
assert.notEqual(staticInner, StaticClosure.method());
assert.equal(C.name, 'MethodSelf0');
assert.equal(C.length, 0);
assert.throws(() => C(), TypeError);
assert.equal(C.prototype.constructor, C);
assert.equal(Object.getPrototypeOf(C), Function.prototype);
assert.equal(Object.getPrototypeOf(C.prototype), Object.prototype);
assert.deepEqual(Reflect.ownKeys(C), ['length', 'name', 'prototype', 'method']);
assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor', 'method']);
assert.notEqual(C.method, C.prototype.method);
for (const receiver of [C, C.prototype]) {
  const method = receiver.method;
  assert.equal(method.name, 'method');
  assert.equal(method.length, 1);
  assert.equal(Object.hasOwn(method, 'prototype'), false);
  assert.throws(() => new method(), TypeError);
  assert.deepEqual(Object.getOwnPropertyDescriptor(receiver, 'method'), {
    value: method, writable: true, enumerable: false, configurable: true
  });
  assert.equal(method.call(undefined), method);
  receiver.method = () => 42;
  assert.equal(method.call({}), method);
  delete receiver.method;
  assert.equal(method.call(null), method);
}
assert.equal(Object.getPrototypeOf(new C()), C.prototype);
