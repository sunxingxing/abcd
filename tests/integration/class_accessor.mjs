import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const source = await import(pathToFileURL(process.argv[2]));
for (const owner of [source.InstanceAccessor.prototype, source.StaticAccessor]) {
  const descriptor = Object.getOwnPropertyDescriptor(owner, 'value');
  assert.equal(descriptor.enumerable, false);
  assert.equal(descriptor.configurable, true);
  assert.equal(descriptor.get.name, 'get value');
  assert.equal(descriptor.get.length, 0);
  assert.equal(descriptor.set.name, 'set value');
  assert.equal(descriptor.set.length, 1);
  assert.equal(Object.hasOwn(descriptor.get, 'prototype'), false);
  assert.equal(Object.hasOwn(descriptor.set, 'prototype'), false);
  assert.throws(() => Reflect.construct(descriptor.get, []), TypeError);
  assert.throws(() => Reflect.construct(descriptor.set, []), TypeError);
}
const instance = new source.InstanceAccessor();
instance.value = 5;
assert.equal(instance.value, undefined);
source.StaticAccessor.value = 5;
assert.equal(source.StaticAccessor.value, undefined);
