import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const load = option => {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  assert.ok(source.includes('value:') && source.includes('done:'));
  assert.ok(!source.includes('.call(') && !source.includes('Reflect.'));
  return vm.runInNewContext(source);
};
const nested = load('--nested-source'), buffered = load('--nested-buffer-source'), call = load('--call-source');
const values = [undefined, null, false, true, 0, -0, 1, NaN, Infinity,
  '', 'text', 0n, 1n, Symbol('identity'), {}, [], () => {}];
function record(result, value, done) {
  assert.ok(Object.is(result.value, value)); assert.equal(result.done, done === true);
  assert.deepEqual(Reflect.ownKeys(result), ['value', 'done']);
  const prototype = Object.getPrototypeOf(result);
  assert.equal(Object.getPrototypeOf(prototype), null);
  for (const key of ['value', 'done']) {
    const property = Object.getOwnPropertyDescriptor(result, key);
    assert.deepEqual([property.writable, property.enumerable, property.configurable], [true, true, true]);
  }
}
function reference(input, flag, receiver, isCall, isBuffer) {
  try {
    if (isCall) {
      const method = receiver.result;
      const result = {};
      Object.defineProperty(result, 'value', {value: input.value, writable: true, enumerable: true, configurable: true});
      Object.defineProperty(result, 'done', {value: input.done === true, writable: true, enumerable: true, configurable: true});
      return Reflect.apply(method, receiver, [result]);
    }
    const outer = {};
    if (isBuffer) Object.defineProperty(outer, 'result', {value: null, writable: true, enumerable: true, configurable: true});
    const result = {};
    Object.defineProperty(result, 'value', {value: input.value, writable: true, enumerable: true, configurable: true});
    Object.defineProperty(result, 'done', {value: input.done === true, writable: true, enumerable: true, configurable: true});
    Object.defineProperty(outer, 'result', {value: result, writable: true, enumerable: true, configurable: true});
    return outer;
  } catch (error) {return error;}
}
function run(fn, value, done, fail, isCall, isBuffer) {
  const trace = []; let inner;
  const method = function(result) {
    trace.push(['invoke', this === receiver]); inner = result;
    if (fail === 3) throw value;
    return result;
  };
  Object.defineProperty(method, 'call', {get() {assert.fail('mutable call helper');}});
  const receiver = {get result() {trace.push(['callee']); if (fail === 4) throw value; return method;}};
  const input = {get value() {
    trace.push(['value']);
    Object.defineProperty(receiver, 'result', {value() {assert.fail('callee re-read');}, configurable: true});
    if (fail === 1) throw value; return value;
  }, get done() {trace.push(['done']); if (fail === 2) throw value; return done;}};
  const actual = fn(input, undefined, receiver, isCall, isBuffer);
  if (fail) assert.ok(Object.is(actual, value));
  else {
    const result = isCall ? actual : actual.result;
    record(result, value, done);
    if (isCall) assert.equal(actual, inner);
    else {
      assert.deepEqual(Reflect.ownKeys(actual), ['result']);
      const property = Object.getOwnPropertyDescriptor(actual, 'result');
      assert.deepEqual([property.writable, property.enumerable, property.configurable], [true, true, true]);
    }
  }
  return trace;
}
let cases = 0;
for (const value of values) for (const done of values)
  for (const [fn, isCall, isBuffer] of [[nested, false, false], [buffered, false, true], [call, true, false]])
    for (const fail of (isCall ? [0, 1, 2, 3, 4] : [0, 1, 2])) {
      const expected = run(reference, value, done, fail, isCall, isBuffer);
      const actual = run(fn, value, done, fail, isCall, isBuffer);
      assert.deepEqual(actual, expected); ++cases;
    }
for (const fn of [nested, buffered]) {
  const one = fn({value: null, done: false}), two = fn({value: null, done: false});
  assert.notEqual(one, two); assert.notEqual(one.result, two.result);
  const prototype = Object.getPrototypeOf(one);
  Object.defineProperty(prototype, 'result', {set() {assert.fail('inherited setter');}, configurable: true});
  try {record(fn({value: null, done: false}).result, null, false);} finally {delete prototype.result;}
}
console.log(`${cases} composed iterator-result identity, getter, receiver and exception cases passed`);
