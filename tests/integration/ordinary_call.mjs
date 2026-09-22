import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const emitted = await import(pathToFileURL(process.argv[2]).href);
const a = {}, b = Symbol(), c = 7n;
const expected = [[], [a], [b, a], [a, a, c], [a, b, c], []];
for (let i = 0; i < expected.length; ++i) {
  const call = emitted[`call${i}`];
  let count = 0;
  const value = {};
  function target(...args) {
    assert.equal(this, undefined);
    assert.equal(new.target, undefined);
    assert.deepEqual(args, expected[i]);
    ++count;
    return value;
  }
  Object.defineProperty(target, 'call', {get() {throw new Error('call lookup');}});
  Object.defineProperty(target, 'apply', {get() {throw new Error('apply lookup');}});
  assert.equal(call(target, a, b, c), value);
  assert.equal(count, 1);
  const proxy = new Proxy(target, {apply(fn, receiver, args) {
    assert.equal(receiver, undefined);
    assert.deepEqual(args, expected[i]);
    return value;
  }});
  assert.equal(call(proxy, a, b, c), value);
  const receiver = {};
  const bound = function(...args) {
    assert.equal(this, receiver);
    assert.deepEqual(args, [value, ...expected[i]]);
    return -0;
  }.bind(receiver, value);
  assert.equal(call(bound, a, b, c), -0);
  const error = {};
  const throwing = () => {throw error;};
  assert.throws(() => call(throwing, a, b, c), value => value === error);
  const failed = emitted[`failure${i}`](throwing, a, b, c);
  assert.deepEqual(failed, [throwing, throwing, error]);
  const revoked = Proxy.revocable(target, {});
  revoked.revoke();
  assert.throws(() => call(revoked.proxy, a, b, c), TypeError);
  let coercions = 0;
  const object = {
    [Symbol.toPrimitive]() {++coercions; throw error;},
    get toString() {++coercions; throw error;}
  };
  for (const invalid of [object, null, undefined, 3, Symbol(), class {}])
    assert.throws(() => call(invalid, a, b, c), TypeError);
  assert.equal(coercions, 0);
}
for (const count of [200, 201, 255]) {
  const args = Array.from({length: count}, (_, i) => i);
  assert.equal(emitted[`range${count}`](function(...actual) {
    assert.equal(this, undefined);
    assert.deepEqual(actual, args);
    return count;
  }, ...args), count);
}
