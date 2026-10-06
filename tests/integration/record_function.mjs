import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const sync = await import(pathToFileURL(process.argv[2]).href);
const async = await import(pathToFileURL(process.argv[3]).href);
const values = [undefined, null, false, 0, -0, NaN, Infinity, 42, 1n, Symbol('value'), {}, []];
for (const fn of [sync.identity, async.identity]) {
  assert.equal(fn.name, 'identity'); assert.equal(fn.length, 1);
  const descriptor = Object.getOwnPropertyDescriptor(fn, 'length');
  assert.equal(descriptor.writable, false); assert.equal(descriptor.enumerable, false);
  assert.equal(descriptor.configurable, true);
}
for (const value of values) {
  assert.ok(Object.is(sync.identity(value), value));
  const promise = async.identity(value);
  assert.ok(promise instanceof Promise);
  assert.ok(Object.is(await promise, value));
  const instance = new sync.identity(value);
  if (value !== null && typeof value === 'object') assert.equal(instance, value);
  else assert.equal(Object.getPrototypeOf(instance), sync.identity.prototype);
}
assert.throws(() => Reflect.construct(async.identity, []), {name: 'TypeError'});
let assimilated = 0;
const token = {};
assert.equal(await async.identity({then(resolve) { ++assimilated; resolve(token); }}), token);
assert.equal(assimilated, 1);
const sentinel = {};
await assert.rejects(async.identity({get then() {throw sentinel;}}), error => error === sentinel);
console.log('value-tree record functions: identity, descriptors, construction, async assimilation passed');
