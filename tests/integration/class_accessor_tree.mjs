import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';
const [tool] = process.argv.slice(2);
for (const placement of ['prototype', 'static', 'retained']) {
  const run = spawnSync(tool, [`--${placement}`], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr);
  assert.match(run.stdout, /get value\(\)/);
  assert.match(run.stdout, /set value\(/);
  assert.doesNotMatch(run.stdout, /\n\s*(?:"value"|false);/);
  const rawCreate = vm.runInNewContext(`"use strict"; ${run.stdout}`);
  const create = placement === 'retained' ? () => rawCreate().constructor : rawCreate;
  // Independent native reference for the fixture bytecode, never an engine input.
  const reference = placement !== 'static' ? () => class Accessors {
    get value() { return this._value; }
    set value(value) { this._value = value; }
    get value() { return this._value; }
    set value(value) { this._value = value; }
  } : () => class Accessors {
    static get value() { return this._value; }
    static set value(value) { this._value = value; }
    static get value() { return this._value; }
    static set value(value) { this._value = value; }
  };
  function observe(factory) {
    const C = factory(), D = factory();
    assert.notEqual(C, D); assert.notEqual(C.prototype, D.prototype);
    const home = placement !== 'static' ? C.prototype : C;
    const other = placement !== 'static' ? D.prototype : D;
    const descriptor = Object.getOwnPropertyDescriptor(home, 'value');
    assert.notEqual(descriptor.get, Object.getOwnPropertyDescriptor(other, 'value').get);
    const object = placement !== 'static' ? new C() : C;
    const value = {}; object.value = value; assert.equal(object.value, value);
    const events = [], sentinel = {};
    const receiver = new Proxy({}, {
      get(target, key, actualReceiver) { events.push(['get', key]); assert.equal(actualReceiver, receiver); return sentinel; },
      set(target, key, input, actualReceiver) { events.push(['set', key]); assert.equal(actualReceiver, receiver); assert.equal(input, value); return true; },
    });
    assert.equal(Reflect.apply(descriptor.get, receiver, []), sentinel);
    assert.equal(Reflect.apply(descriptor.set, receiver, [value]), undefined);
    assert.deepEqual(events, [['get', '_value'], ['set', '_value']]);
    for (const kind of ['get', 'set']) {
      const throwing = new Proxy({}, {[kind]() { throw sentinel; }});
      assert.throws(() => Reflect.apply(descriptor[kind], throwing, [value]), e => e === sentinel);
      assert.throws(() => Reflect.construct(descriptor[kind], []), e => e.name === 'TypeError');
      assert.equal(Object.hasOwn(descriptor[kind], 'prototype'), false);
    }
    assert.throws(() => Reflect.apply(descriptor.set, Object.freeze({}), [value]), e => e.name === 'TypeError');
    return {name: C.name, length: C.length, keys: Reflect.ownKeys(home),
      getter: descriptor.get.name, setter: descriptor.set.name,
      arity: [descriptor.get.length, descriptor.set.length],
      enumerable: descriptor.enumerable, configurable: descriptor.configurable};
  }
  assert.deepEqual(observe(create), observe(reference));
}
