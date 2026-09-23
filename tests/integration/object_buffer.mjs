import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {empty, emptyField, definedField, indexedField, stringKeyField, stringProtoField, object, proto, primitive, field, redefine,
       intervening, ownProto, preserved} =
    await import(pathToFileURL(process.argv[2]));
const emptyFirst = empty();
assert.notEqual(emptyFirst, empty());
assert.deepEqual(Reflect.ownKeys(emptyFirst), []);
assert.equal(Object.getPrototypeOf(emptyFirst), Object.prototype);
assert.deepEqual(emptyField(12), {field: 12});
for (const value of [undefined, null, 12, Symbol('field'), {}]) {
  const result = definedField(value);
  assert.deepEqual(Reflect.ownKeys(result), ['field']);
  assert.deepEqual(Object.getOwnPropertyDescriptor(result, 'field'), {
    value, writable: true, enumerable: true, configurable: true,
  });
}
for (const value of [undefined, 0, -0, NaN, Symbol('indexed'), {}]) {
  const result = indexedField(value);
  assert.deepEqual(Reflect.ownKeys(result), ['201', '4294967295']);
  for (const key of ['201', '4294967295']) {
    assert.deepEqual(Object.getOwnPropertyDescriptor(result, key), {
      value, writable: true, enumerable: true, configurable: true,
    });
  }
}
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
  const defined = definedField(43);
  const keyed = stringKeyField(44);
  assert.equal(setterCalls, 0);
  assert.equal(defined.field, 43);
  assert.equal(keyed.field, 44);
  assert.deepEqual(Object.getOwnPropertyDescriptor(keyed, 'field'), {
    value: 44, writable: true, enumerable: true, configurable: true,
  });
  const protoField = stringProtoField(45);
  assert.equal(Object.getPrototypeOf(protoField), Object.prototype);
  assert.equal(Object.getOwnPropertyDescriptor(protoField, '__proto__').value,
               45);
  assert.deepEqual(Reflect.ownKeys(result), ['text', 'field']);
  assert.deepEqual(Object.getOwnPropertyDescriptor(result, 'field'), {
    value: 42, writable: true, enumerable: true, configurable: true,
  });
} finally {
  delete Object.prototype.field;
}
assert.deepEqual(redefine(7, 9), {text: 9, field: 7});
assert.deepEqual(Reflect.ownKeys(redefine(7, 9)), ['text', 'field']);
const value = {count: 1};
const observed = [];
const withEffect = intervening(value, 9, () => {
  observed.push('called');
  value.count = 2;
});
assert.deepEqual(observed, ['called']);
assert.equal(withEffect.field, value);
assert.equal(withEffect.field.count, 2);
assert.deepEqual(Reflect.ownKeys(withEffect), ['text', 'field']);
const failure = new Error('effect failed');
assert.throws(() => intervening(value, 9, () => { throw failure; }),
              error => error === failure);
const withProto = ownProto(17);
assert.equal(Object.getPrototypeOf(withProto), Object.prototype);
assert.equal(Object.getOwnPropertyDescriptor(withProto, '__proto__').value, 17);
assert.equal(preserved(31), 31);
