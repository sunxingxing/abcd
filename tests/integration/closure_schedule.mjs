import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE'));
const generated = runInNewContext(`'use strict'; (${source})`);
function reference(left, right, consume) {
  let result;
  for (let i = 0; i < 2; ++i) {
    consume(function readLeft() { return left; });
    result = {
      nested: function readRight() { return right; },
      write: function writeBoth(value) { left = value; right = value; return value; }
    };
    consume(result);
  }
  return result;
}
const failure = {}, token = {}, update = {};
const hostile = {[Symbol.toPrimitive]() { assert.fail('capture reads must not coerce'); }};
const values = [undefined, null, false, -0, NaN, 1, 2n, Symbol('value'), token, hostile];
function execute(factory, left, right, options) {
  const events = [], getters = [], objects = [], functions = new Set();
  let calls = 0, result, threw = false;
  const callable = (fn, name, length) => {
    assert.equal(typeof fn, 'function'); assert.ok(!functions.has(fn)); functions.add(fn);
    assert.equal(fn.name, name); assert.equal(fn.length, length);
    const descriptor = Object.getOwnPropertyDescriptor(fn, 'name');
    assert.equal(descriptor.writable, false); assert.equal(descriptor.enumerable, false);
    assert.equal(descriptor.configurable, true);
  };
  const snapshot = () => [getters.map(fn => fn()), objects.map(object => object.nested())];
  try {
    result = factory(left, right, function(value) {
      assert.equal(this, undefined); ++calls;
      if (typeof value === 'function') {
        callable(value, 'readLeft', 0); getters.push(value);
        events.push(['function', value()]);
        if (options.writeBeforeLiteral && objects.length) assert.equal(objects.at(-1).write(update), update);
      } else {
        assert.deepEqual(Object.keys(value), ['nested', 'write']); assert.ok(!objects.includes(value));
        callable(value.nested, 'readRight', 0); callable(value.write, 'writeBoth', 1); objects.push(value);
        for (const key of ['nested', 'write']) {
          const descriptor = Object.getOwnPropertyDescriptor(value, key);
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
        }
        events.push(['object', value.nested()]);
        if (options.writeDuring) assert.equal(value.write(update), update);
      }
      events.push(['state', snapshot()]);
      if (calls === options.throwAt) throw failure;
    });
    assert.equal(result, objects.at(-1));
  } catch (error) { assert.equal(error, failure); threw = true; }
  // Escaped callbacks remain attached to the same mutable cells after either
  // normal return or an injected throw from the consuming call.
  for (const object of objects) {
    assert.equal(object.write(token), token);
    events.push(['after', snapshot()]);
  }
  return {events, threw};
}
let scenarios = 0;
for (const left of values) for (const right of values)
  for (const options of [{}, {writeDuring: true}, {writeBeforeLiteral: true},
    ...[1, 2, 3, 4].flatMap(throwAt => [{throwAt}, {throwAt, writeDuring: true}])]) {
    assert.deepEqual(execute(generated, left, right, options), execute(reference, left, right, options));
    ++scenarios;
  }
for (const factory of [generated, reference]) {
  const a = [], b = [];
  const first = factory(token, update, value => a.push(value));
  const second = factory(failure, hostile, value => b.push(value));
  assert.notEqual(first, second); assert.notEqual(a[0], b[0]);
  first.write(undefined);
  assert.equal(a[0](), undefined); assert.equal(first.nested(), undefined);
  assert.equal(b[0](), failure); assert.equal(second.nested(), hostile);
  second.write(null);
  assert.equal(a[0](), undefined); assert.equal(b[0](), null);
}
console.log(`Closure construction schedule: ${scenarios} independent capture/identity scenarios passed`);
