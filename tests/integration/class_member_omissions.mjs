import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';

const [tool] = process.argv.slice(2);
const run = spawnSync(tool, ['--partial-json'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const results = run.stdout.trim().split('\n').map(line => JSON.parse(line));
assert.equal(results.length, 6);
let cases = 0;
for (const result of results) {
  assert.equal(result.incomplete, true);
  assert.match(result.source, /INCOMPLETE class/);
  assert.equal((result.source.match(/UNRECOVERED class definition/g) ?? []).length, 2);
  assert.doesNotMatch(result.source, /get value\(/);
  const create = vm.runInNewContext(`"use strict"; ${result.source}`);
  // This independent reference describes only the retained descriptor halves.
  // Original getters are explicitly absent, so no whole-class equivalence or
  // record/constructor acceptance is inferred from these method observations.
  const reference = result.static ? () => class Accessors {
    static set value(value) { this._value = value; }
    static set value(value) { this._value = value; }
  } : () => class Accessors {
    set value(value) { this._value = value; }
    set value(value) { this._value = value; }
  };
  for (const value of [undefined, null, false, true, 0, -0, NaN, Infinity, '', 'abc', {}, [], Symbol('input'), 1n]) {
    const observe = factory => {
      const C = factory();
      const home = result.static ? C : C.prototype;
      const descriptor = Object.getOwnPropertyDescriptor(home, 'value');
      assert.equal(descriptor.get, undefined);
      const fn = descriptor.set;
      assert.equal(fn.name, 'set value');
      assert.equal(fn.length, 1);
      assert.equal(Object.hasOwn(fn, 'prototype'), false);
      assert.throws(() => Reflect.construct(fn, []), error => error.name === 'TypeError');
      for (const receiver of [null, undefined, 0, 'text', true, Object.freeze({})])
        assert.throws(() => Reflect.apply(fn, receiver, [value]), error => error.name === 'TypeError');
      const trace = [];
      const receiver = new Proxy({}, {set(target, key, input, actual) {
        assert.equal(actual, receiver); assert.equal(input, value);
        trace.push(key); return true;
      }});
      assert.equal(Reflect.apply(fn, receiver, [value]), undefined);
      const token = {};
      assert.throws(() => Reflect.apply(fn, new Proxy({}, {set() {throw token;}}), [value]), error => error === token);
      assert.throws(() => Reflect.apply(fn, new Proxy({}, {set() {return false;}}), [value]), error => error.name === 'TypeError');
      return {trace, enumerable: descriptor.enumerable, configurable: descriptor.configurable};
    };
    assert.deepEqual(observe(create), observe(reference)); ++cases;
  }
}
console.log(`${cases} retained accessor scenarios passed; omitted getters and incomplete class retained`);
