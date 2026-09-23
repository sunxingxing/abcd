import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {object, proto, primitive, field, redefine, ownProto, preserved} =
    await import(pathToFileURL(process.argv[2]));
const first = object();
const second = object();
assert.notEqual(first, second);
assert.deepEqual(Reflect.ownKeys(first), ['text']);
assert.equal(first.text, null);
assert.equal(Object.getPrototypeOf(first), Object.prototype);
assert.deepEqual(Object.getOwnPropertyDescriptor(first, 'text'), {
  value: null, writable: true, enumerable: true, configurable: true,
});
const originalProto = proto();
assert.equal(Object.getPrototypeOf(originalProto), Object.prototype);
assert.equal(Object.hasOwn(originalProto, '__proto__'), true);
assert.equal(originalProto.__proto__, null);
assert.deepEqual(primitive(), {
  bool: true, uint: 4294967295, double: 1.5, str: 'hello',
});
let setterCalls = 0;
Object.defineProperty(Object.prototype, 'field', {
  set() { setterCalls++; }, configurable: true,
});
try {
  const result = field(42);
  assert.equal(setterCalls, 0);
  assert.deepEqual(Reflect.ownKeys(result), ['text', 'field']);
  assert.deepEqual(Object.getOwnPropertyDescriptor(result, 'field'), {
    value: 42, writable: true, enumerable: true, configurable: true,
  });
} finally {
  delete Object.prototype.field;
}
assert.deepEqual(redefine(7, 9), {text: 9, field: 7});
const withProto = ownProto(17);
assert.equal(Object.getPrototypeOf(withProto), Object.prototype);
assert.equal(Object.getOwnPropertyDescriptor(withProto, '__proto__').value, 17);
assert.equal(preserved(31), 31);
