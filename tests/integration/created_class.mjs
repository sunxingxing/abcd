import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {recovered: C} = await import(pathToFileURL(process.argv[2]));
assert.equal(C.name, 'Recovered');
assert.equal(C.length, 1);
assert.equal(Object.getPrototypeOf(C), Function.prototype);
assert.equal(Object.getPrototypeOf(C.prototype), Object.prototype);
assert.equal(C.prototype.constructor, C);
assert.deepEqual(Reflect.ownKeys(C), ['length', 'name', 'prototype']);
assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor']);
assert.deepEqual(Object.getOwnPropertyDescriptor(C, 'prototype'), {
  value: C.prototype, writable: false, enumerable: false, configurable: false
});
assert.throws(() => C(), TypeError);
for (const value of [undefined, null, 0, 1n, 'text', Symbol(), {}, () => {}]) {
  const instance = new C(value);
  if (value !== null && ['object', 'function'].includes(typeof value))
    assert.equal(instance, value);
  else assert.equal(Object.getPrototypeOf(instance), C.prototype);
}
