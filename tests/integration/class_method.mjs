import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const source = await import(pathToFileURL(process.argv[2]));
for (const [key, C] of Object.entries(source)) {
  const owner = key === 'staticSelf' ? C : C.prototype;
  const name = key === 'escaped' ? 'quoted"key' : 'method';
  const method = owner[name];
  assert.equal(method.name, name);
  assert.equal(method.length, 1);
  assert.equal(Object.hasOwn(method, 'prototype'), false);
  assert.throws(() => Reflect.construct(method, []), TypeError);
  assert.deepEqual(Object.getOwnPropertyDescriptor(owner, name), {
    value: method, writable: true, enumerable: false, configurable: true
  });
}
for (const value of [undefined, null, 42, 1n, Symbol(), {}, () => {}]) {
  assert.equal(source.parameter.prototype.method(value), value);
  assert.equal(source.escaped.prototype['quoted"key'](value), value);
  assert.equal(Reflect.apply(source.receiver.prototype.method, value, []), value);
  assert.throws(() => source.thrown.prototype.method(value), error => Object.is(error, value));
}
assert.equal(source.newTarget.prototype.method(), undefined);
for (const owner of [source.self.prototype, source.staticSelf]) {
  const original = owner.method;
  owner.method = () => 42;
  assert.equal(original(), original);
  delete owner.method;
  assert.equal(original(), original);
}
