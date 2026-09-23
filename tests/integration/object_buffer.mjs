import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {object, proto, primitive} = await import(pathToFileURL(process.argv[2]));
const first = object();
const second = object();
assert.notEqual(first, second);
assert.deepEqual(Reflect.ownKeys(first), ['text']);
assert.equal(first.text, null);
assert.equal(Object.getPrototypeOf(first), Object.prototype);
assert.deepEqual(Object.getOwnPropertyDescriptor(first, 'text'), {
  value: null, writable: true, enumerable: true, configurable: true,
});
const ownProto = proto();
assert.equal(Object.getPrototypeOf(ownProto), Object.prototype);
assert.equal(Object.hasOwn(ownProto, '__proto__'), true);
assert.equal(ownProto.__proto__, null);
assert.deepEqual(primitive(), {
  bool: true, uint: 4294967295, double: 1.5, str: 'hello',
});
