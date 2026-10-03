import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const load = option => {
  const text = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  assert.ok(text.includes('value:') && text.includes('done:'));
  const trap = new Proxy(function() {}, {apply() {assert.fail('mutable global factory');},
    construct() {assert.fail('mutable global constructor');}});
  return vm.runInNewContext(text, {Object: trap, Boolean: trap});
};
const direct = load('--source'), boolean = load('--boolean-source');
const computed = load('--computed-source'), caught = load('--caught-source');
const payloads = [undefined, null, false, true, 0, -0, 1, NaN, Infinity,
  '', 'text', 0n, 1n, Symbol('identity'), {}, [], () => {}];
function checkObject(result, value, done) {
  assert.ok(Object.is(result.value, value)); assert.equal(result.done, done);
  // Objects originate in the emitted function's realm. Its original prototype
  // must have exactly Object.prototype's own constructor and ordinary chain.
  const proto = Object.getPrototypeOf(result);
  assert.equal(Object.getPrototypeOf(proto), null);
  assert.equal(proto.constructor.name, 'Object');
  assert.deepEqual(Reflect.ownKeys(result), ['value', 'done']);
  for (const key of ['value', 'done']) {
    const property = Object.getOwnPropertyDescriptor(result, key);
    assert.deepEqual([property.writable, property.enumerable, property.configurable], [true, true, true]);
  }
}
let cases = 0;
for (const value of payloads) for (const done of payloads) {
  checkObject(direct(value, done), value, done === true);
  checkObject(boolean(value, done), value, !!done);
  assert.notEqual(direct(value, done), direct(value, done));
  cases += 2;
  for (const fail of [0, 1, 2]) for (const fn of [computed, caught]) {
    const trace = [];
    const input = new Proxy({}, {get(target, key, receiver) {
      trace.push([key, receiver === input]);
      if (fail === (key === 'value' ? 1 : 2)) throw value;
      return key === 'value' ? value : done;
    }});
    let result, thrown = false;
    try {result = fn(input);} catch (error) {thrown = true; result = error;}
    assert.deepEqual(trace, fail === 1 ? [['value', true]] : [['value', true], ['done', true]]);
    if (fail) {
      assert.equal(thrown, fn === computed); assert.ok(Object.is(result, value));
    } else {assert.equal(thrown, false); checkObject(result, value, done === true);}
    ++cases;
  }
}
// IsTrue must not call user coercion, and data-property creation must not
// trigger inherited setters. Operate in the emitted function's actual realm.
const prototype = Object.getPrototypeOf(direct(null, false));
for (const key of ['value', 'done']) Object.defineProperty(prototype, key, {
  set() {assert.fail('inherited setter');}, configurable: true,
});
try {
  const poison = new Proxy({}, {get() {assert.fail('flag coercion');}});
  checkObject(direct(poison, poison), poison, false);
  checkObject(boolean(poison, poison), poison, true);
} finally {delete prototype.value; delete prototype.done;}
console.log(`${cases} iterator-result identity, property, order and exception cases passed`);
