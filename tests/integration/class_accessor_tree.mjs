import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';
const [tool] = process.argv.slice(2);
for (const placement of ['prototype', 'static', 'retained', 'method-last', 'accessor-last']) {
  const run = spawnSync(tool, [`--${placement}`], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr);
  if (placement !== 'method-last') assert.match(run.stdout, /set value\(/);
  if (placement !== 'accessor-last') assert.match(run.stdout, /get value\(\)/);
  if (placement.endsWith('-last')) assert.match(run.stdout, /\n\s+value\(/);
  assert.doesNotMatch(run.stdout, /\n\s*(?:"value"|false);/);
  const rawCreate = vm.runInNewContext(`"use strict"; ${run.stdout}`);
  const create = placement === 'retained' ? () => rawCreate().constructor : rawCreate;
  // Independent native reference for the fixture bytecode, never an engine input.
  const reference = placement === 'method-last' ? () => class Accessors {
    get value() { return this._value; }
    value(value) { this._value = value; }
    get value() { return this._value; }
    value(value) { this._value = value; }
  } : placement === 'accessor-last' ? () => class Accessors {
    value() { return this._value; }
    set value(value) { this._value = value; }
    value() { return this._value; }
    set value(value) { this._value = value; }
  } : placement !== 'static' ? () => class Accessors {
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
    const object = placement !== 'static' ? new C() : C;
    const value = {};
    if (descriptor.value) object.value(value); else object.value = value;
    assert.equal(object._value, value);
    if (descriptor.get) assert.equal(object.value, value);
    if (placement === 'accessor-last') assert.equal(descriptor.get, undefined);
    if (placement === 'method-last') {
      assert.equal(Object.hasOwn(descriptor, 'get'), false);
      assert.equal(Object.hasOwn(descriptor, 'set'), false);
      assert.equal(descriptor.writable, true);
    }
    const sentinel = {}, results = [];
    for (const kind of ['get', 'set', 'value']) {
      const fn = descriptor[kind]; if (!fn) continue;
      assert.notEqual(fn, Object.getOwnPropertyDescriptor(other, 'value')[kind]);
      const operation = kind === 'get' ? 'get' : 'set';
      const events = [];
      const receiver = new Proxy({}, {
        get(target, key, actualReceiver) { events.push(['get', key]); assert.equal(actualReceiver, receiver); return sentinel; },
        set(target, key, input, actualReceiver) { events.push(['set', key]); assert.equal(actualReceiver, receiver); assert.equal(input, value); return true; },
      });
      assert.equal(Reflect.apply(fn, receiver, [value]), kind === 'get' ? sentinel : undefined);
      assert.deepEqual(events, [[operation, '_value']]);
      const throwing = new Proxy({}, {[operation]() { throw sentinel; }});
      assert.throws(() => Reflect.apply(fn, throwing, [value]), e => e === sentinel);
      assert.throws(() => Reflect.construct(fn, []), e => e.name === 'TypeError');
      assert.equal(Object.hasOwn(fn, 'prototype'), false);
      if (operation === 'set') assert.throws(() => Reflect.apply(fn, Object.freeze({}), [value]), e => e.name === 'TypeError');
      results.push({kind, name: fn.name, length: fn.length});
    }
    return {name: C.name, length: C.length, keys: Reflect.ownKeys(home), members: results,
      enumerable: descriptor.enumerable, configurable: descriptor.configurable};
  }
  assert.deepEqual(observe(create), observe(reference));
}
