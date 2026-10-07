import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';
const [tool] = process.argv.slice(2);
// Decode real class/method/call bytes with pure splits at both boundaries.
// The independent native oracle enters only after engine source generation.
for (const [flag, captured] of [['--initializers-json', false], ['--self-initializers-json', true], ['--frame-initializers-json', false], ['--body-initializers-json', false], ['--closed-initializers-json', true]]) {
  const run = spawnSync(tool, [flag], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr);
  let scenarios = 0;
  for (const line of run.stdout.trim().split('\n')) {
    const {source, self = captured, fields = false, string_value = false, compound = false} = JSON.parse(line);
    const initialValue = compound ? !(typeof 'value' === typeof '_value') : string_value ? 'value' : 42;
    if (compound) {
      assert.match(source, /static _value =/);
      assert.match(source, /typeof "value"/); assert.match(source, /typeof "_value"/);
      assert.match(source, /static _value = typeof "value" !== typeof "_value";/);
    } else assert.match(source, string_value ? /static _value = "value";/ : /static _value = 42;/);
    if (fields) {
      assert.match(source, /static _other = 17;/);
      assert.ok(source.indexOf('static _other') < source.indexOf('static _value'));
    }
    assert.doesNotMatch(source, /INCOMPLETE|\.call\(|Object\.defineProperty/);
    const create = vm.runInNewContext(`'use strict'; ${source}`);
    // Actual captured constructors read the outer cell; the oracle enters only
    // after generation and is never supplied to the bytecode recovery engine.
    const reference = fields ? self ? () => {
      let C = class Accessors {constructor() {return C;} static _other = 17; static _value = initialValue;};
      return C;
    } : () => class Accessors {static _other = 17; static _value = initialValue;} : self ? () => {
      let C = class Accessors {constructor() {return C;} static _value = 42;};
      return C;
    } : () => class Accessors {static _value = 42;};
    for (const input of [undefined, null, false, true, 0, -0, NaN, 1, 0n, 1n, '', 'x', Symbol(), {}, []]) {
      const describe = factory => {
        const C = factory(input), D = factory(input);
        assert.notEqual(C, D); assert.notEqual(C.prototype, D.prototype);
        assert.equal(C._value, initialValue);
        if (fields) {
          assert.equal(C._other, 17);
          const other = Object.getOwnPropertyDescriptor(C, '_other');
          assert.deepEqual({value: other.value, writable: other.writable, enumerable: other.enumerable, configurable: other.configurable},
            {value: 17, writable: true, enumerable: true, configurable: true});
          assert.equal(new C()._other, self ? 17 : undefined);
        }
        if (self) {
          assert.equal(new C(), C); assert.equal(new D(), D);
          assert.equal(Reflect.construct(C, []), C);
        } else assert.equal(new C()._value, undefined);
        const descriptor = Object.getOwnPropertyDescriptor(C, '_value');
        const value = {};
        assert.equal(Reflect.set(C, '_value', value), true); assert.equal(C._value, value);
        assert.equal(new C()._value, self ? value : undefined); assert.equal(D._value, initialValue);
        return {name: C.name, length: C.length, keys: Reflect.ownKeys(C), prototypeKeys: Reflect.ownKeys(C.prototype),
          value: descriptor.value, writable: descriptor.writable, enumerable: descriptor.enumerable, configurable: descriptor.configurable};
      };
      assert.deepEqual(describe(create), describe(reference)); ++scenarios;
    }
  }
  console.log(`${flag} native comparisons: ${scenarios}`);
}
for (const placement of ['prototype', 'static', 'retained', 'method-last', 'accessor-last', 'prototype-read', 'computed-prototype-read']) {
  const run = spawnSync(tool, [`--${placement}`], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr);
  if (placement.endsWith('prototype-read')) assert.doesNotMatch(run.stdout, /^.*\.prototype;$/m);
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

// Completed class installations dominate later direct/phi observations. This
// fixture's encoded CFG varies; the oracle never enters the recovery engine.
const observers = spawnSync(tool, ['--observers-json'], {encoding: 'utf8', timeout: 60_000});
assert.equal(observers.status, 0, observers.stderr);
let observerScenarios = 0;
for (const line of observers.stdout.trim().split('\n')) {
  const {source, static: isStatic, selection, ordinary} = JSON.parse(line);
  assert.match(source, /class\s*(?:Accessors\s*)?\{/);
  assert.doesNotMatch(source, /INCOMPLETE/);
  const recovered = vm.runInNewContext(`'use strict'; ${source}`);
  const reference = parameter => {
    const C = ordinary ? (isStatic ? class Accessors {
      static get value() { return this._value; }
      static value(value) { this._value = value; }
      static get value() { return this._value; }
      static value(value) { this._value = value; }
    } : class Accessors {
      get value() { return this._value; }
      value(value) { this._value = value; }
      get value() { return this._value; }
      value(value) { this._value = value; }
    }) : isStatic ? class Accessors {
      static get value() { return this._value; }
      static set value(value) { this._value = value; }
      static get value() { return this._value; }
      static set value(value) { this._value = value; }
    } : class Accessors {
      get value() { return this._value; }
      set value(value) { this._value = value; }
      get value() { return this._value; }
      set value(value) { this._value = value; }
    };
    return selection && (parameter === false || parameter === 0) ? (isStatic ? C : C.prototype) : C;
  };
  const throwing = new Proxy({}, {get() { throw new Error('condition must not coerce'); }});
  for (const parameter of [false, 0, -0, true, 1, NaN, undefined, null, '', '0', {}, throwing]) {
    const observe = factory => {
      const first = factory(parameter), second = factory(parameter);
      assert.notEqual(first, second);
      const isPrototype = typeof first !== 'function';
      const C = isPrototype ? first.constructor : first;
      const home = isStatic ? C : C.prototype;
      const descriptor = Object.getOwnPropertyDescriptor(home, 'value');
      const writer = descriptor.set ?? descriptor.value;
      const events = [], value = {}, sentinel = {};
      const receiver = new Proxy({}, {
        get(target, key, actual) { assert.equal(actual, receiver); events.push(['get', key]); return value; },
        set(target, key, input, actual) {
          assert.equal(actual, receiver); assert.equal(input, value); events.push(['set', key]); return true;
        },
      });
      assert.equal(Reflect.apply(writer, receiver, [value]), undefined);
      if (descriptor.get) assert.equal(Reflect.apply(descriptor.get, receiver, []), value);
      const throws = new Proxy({}, {get() { throw sentinel; }, set() { throw sentinel; }});
      if (descriptor.get) assert.throws(() => Reflect.apply(descriptor.get, throws, []), e => e === sentinel);
      assert.throws(() => Reflect.apply(writer, throws, [value]), e => e === sentinel);
      const otherHome = isStatic ? second : typeof second === 'function' ? second.prototype : second;
      const other = Object.getOwnPropertyDescriptor(otherHome, 'value');
      assert.notEqual(descriptor.get ?? writer, other.get ?? other.value);
      return {isPrototype, name: C.name, keys: Reflect.ownKeys(home), events,
        getter: descriptor.get?.name, setter: descriptor.set?.name, method: descriptor.value?.name,
        length: [descriptor.get?.length, writer.length],
        writable: descriptor.writable, enumerable: descriptor.enumerable, configurable: descriptor.configurable};
    };
    assert.deepEqual(observe(recovered), observe(reference)); ++observerScenarios;
  }
}
console.log(`class observer scenarios: ${observerScenarios}`);

// This bytecode fixture captures one initialized outer cell in the constructor,
// class-buffer method, its nested function, and repeated getter/setter roots.
// The oracle is independent native syntax; the engine never reads it.
const captured = spawnSync(tool, ['--capture-json'], {encoding: 'utf8', timeout: 60_000});
assert.equal(captured.status, 0, captured.stderr);
let captureScenarios = 0;
for (const line of captured.stdout.trim().split('\n')) {
  const {source, static: isStatic} = JSON.parse(line);
  const create = vm.runInNewContext(`"use strict"; ${source}`);
  const reference = () => {
    let cell = 42;
    const nested = () => ({nested: function () { cell = 99; return cell; }}).nested;
    return isStatic ? class Accessors {
      constructor() { return cell; }
      shared() { return nested(); }
      static get value() { return cell; }
      static set value(value) { cell = value; this._value = value; }
      static get value() { return cell; }
      static set value(value) { cell = value; this._value = value; }
    } : class Accessors {
      constructor() { return cell; }
      shared() { return nested(); }
      get value() { return cell; }
      set value(value) { cell = value; this._value = value; }
      get value() { return cell; }
      set value(value) { cell = value; this._value = value; }
    };
  };
  function observe(factory, value, throwStore) {
    const C = factory(), D = factory();
    const home = isStatic ? C : C.prototype, other = isStatic ? D : D.prototype;
    const getter = Object.getOwnPropertyDescriptor(home, 'value').get;
    const setter = Object.getOwnPropertyDescriptor(home, 'value').set;
    assert.equal(Reflect.apply(getter, null, []), 42);
    assert.notEqual(C, D); assert.notEqual(new C(), new C());
    const first = C.prototype.shared(), second = C.prototype.shared();
    assert.notEqual(first, second);
    assert.equal(first.name, 'nested'); assert.equal(first.length, 0);
    assert.equal(first(), 99); assert.equal(Reflect.apply(getter, undefined, []), 99);
    assert.equal(isStatic ? D.value : new D().value, 42);
    const events = [], sentinel = {};
    const receiver = new Proxy({}, {set(target, key, input, actual) {
      assert.equal(actual, receiver); assert.equal(key, '_value'); assert.equal(input, value);
      assert.equal(Reflect.apply(getter, {}, []), value);
      events.push('store after cell write');
      if (throwStore) throw sentinel;
      return Reflect.set(target, key, input);
    }});
    if (throwStore) assert.throws(() => Reflect.apply(setter, receiver, [value]), e => e === sentinel);
    else assert.equal(Reflect.apply(setter, receiver, [value]), undefined);
    assert.equal(Reflect.apply(getter, receiver, []), value);
    assert.equal(isStatic ? D.value : new D().value, 42);
    // Failure after the lexical store must leave that same cell changed.
    assert.throws(() => Reflect.apply(setter, Object.freeze({}), [value]), e => e.name === 'TypeError');
    assert.equal(Reflect.apply(getter, {}, []), value);
    assert.equal(second(), 99); assert.equal(Reflect.apply(getter, {}, []), 99);
    assert.equal(Object.getOwnPropertyDescriptor(other, 'value').get.call(null), 42);
    return {events, name: C.name, getterName: getter.name, setterName: setter.name,
      sharedName: C.prototype.shared.name, keys: Reflect.ownKeys(home)};
  }
  for (const value of [undefined, null, false, 0, NaN, 'text', {}, Symbol('cell')]) {
    for (const throwStore of [false, true]) {
      assert.deepEqual(observe(create, value, throwStore), observe(reference, value, throwStore));
      ++captureScenarios;
    }
  }
}
console.log(`class capture scenarios: ${captureScenarios}`);

// Class snapshot forwarding must preserve name without retaining private storage.
const names = spawnSync(tool,['--class-names'],{encoding:'utf8',timeout:60_000});
assert.equal(names.status,0,names.stderr);
const snapshots = names.stdout.split('\n').filter(line=>line.startsWith('{')).map(JSON.parse);
assert.equal(snapshots.length,2);
for(const test of snapshots) {
  assert.equal(test.snapshot_class,true);
  const factory=vm.runInNewContext(test.source), C=factory(), D=factory();
  assert.equal(C.name,test.name);assert.equal(C.length,0);
  assert.notEqual(C,D);assert.notEqual(C.prototype,D.prototype);
  const instance=new C();assert.equal(Object.getPrototypeOf(instance),C.prototype);
  for(const value of [undefined,null,0,-0,NaN,{},[]]) {instance.value=value;assert.equal(instance.value,value);}
  const descriptor=Object.getOwnPropertyDescriptor(C,'name');
  assert.equal(descriptor.writable,false);assert.equal(descriptor.configurable,true);
  assert.throws(()=>Reflect.apply(C,null,[]),error=>error.name==='TypeError');
}
