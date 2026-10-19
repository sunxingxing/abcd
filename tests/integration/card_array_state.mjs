import assert from 'node:assert/strict';
import vm from 'node:vm';

const Mode = Object.freeze({
  normal: Symbol('normal'),
  reverse: Symbol('reverse'),
  listLive: Symbol('list-live'),
  callLive: Symbol('call-live'),
  itemLive: Symbol('item-live'),
  pushLive: Symbol('push-live'),
  pushMutate: Symbol('push-mutate'),
  reentrant: Symbol('reentrant'),
  listNoncallable: Symbol('list-noncallable'),
  pushNoncallable: Symbol('push-noncallable'),
  itemNonconstructible: Symbol('item-nonconstructible'),
});
const PrototypeKind = Object.freeze({
  class: Symbol('class'),
  foreign: Symbol('foreign'),
  fallback: Symbol('fallback'),
  foreignFallback: Symbol('foreign-fallback'),
});

// Run the complete ABC-emitted module. Host doubles are confined to its two
// dependencies and observable Array.prototype operations; the real constructor,
// arrow closure and method remain in the execution path.
async function observe(source, mode, failureAt = null, failure) {
  const trace = [], points = new Set(), arrays = [], callbacks = [], items = [{}, Symbol('item')];
  let root, base, detail, depth = 0, replacement, nested, callback, inject = true;
  const ids = new WeakMap();
  const arrayId = value => {
    assert.ok(Array.isArray(value));
    if (!ids.has(value)) { ids.set(value, arrays.length); arrays.push(value); }
    return ids.get(value);
  };
  function event(label, ...values) {
    const point = `${depth}:${label}`; trace.push([point, ...values]); points.add(point);
    if (inject && point === failureAt) throw failure;
  }
  const context = vm.createContext({});
  const arrayPrototype = vm.runInContext('Array.prototype', context);
  const push = arrayPrototype.push;
  function pushCall(label, target, values) {
    assert.equal(values.length, 1);
    assert.equal(Object.getPrototypeOf(values[0]), values[0].constructor.prototype);
    event(`push:${label}:call`, arrayId(target), values[0].label, values[0].index);
    return Reflect.apply(push, target, values);
  }
  const latePush = function(...values) { return pushCall('late', this, values); };
  Object.defineProperty(arrayPrototype, 'push', {configurable: true, get() {
    event('push:get', arrayId(this));
    if (mode === Mode.pushLive) detail.setExport('CardItem', replacement);
    if (mode === Mode.pushNoncallable) return null;
    return function(...values) { return pushCall('early', this, values); };
  }});
  const host = label => {
    function CardItem(item, index) {
      assert.equal(new.target, CardItem); assert.equal(item, items[index]);
      event(`item:${label}`, index);
      if (mode === Mode.itemLive) detail.setExport('CardItem', replacement);
      if (mode === Mode.pushMutate) Object.defineProperty(arrays.at(-1), 'push', {
        value: latePush, configurable: true, writable: true,
      });
      this.label = label; this.index = index; this.item = item;
    }
    for (const key of ['call', 'apply', 'bind']) Object.defineProperty(CardItem, key, {get() {
      assert.fail(`unexpected constructor ${key} lookup`);
    }});
    return CardItem;
  };
  const original = host('early'); replacement = host('late');
  const list = label => {
    const owner = {};
    Object.defineProperty(owner, 'forEach', {get() {
      event(`list:${label}:get`);
      if (mode === Mode.listLive) base.setExport('CARD_LIST', secondList);
      if (mode === Mode.reentrant && depth === 0 && !nested) {
        ++depth;
        try { nested = new root.namespace.CardArray(); }
        finally { --depth; }
      }
      if (mode === Mode.listNoncallable) return null;
      const callee = function(fn) {
        assert.equal(this, owner); assert.equal(arguments.length, 1);
        assert.equal(fn.name, ''); assert.equal(fn.length, 2);
        assert.throws(() => Reflect.construct(fn, []), {name: 'TypeError'});
        event(`list:${label}:call`);
        callbacks.push({fn, depth}); if (depth === 0) callback = fn;
        if (mode === Mode.callLive) detail.setExport('CardItem', replacement);
        if (mode === Mode.itemNonconstructible) detail.setExport('CardItem', () => {});
        const order = mode === Mode.reverse ? [1, 0, 1] : [0, 1];
        for (const index of order) Reflect.apply(fn, {cardArray: 'foreign callback receiver'}, [items[index], index]);
        return 'ignored forEach result';
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callee, key, {get() {
        assert.fail(`unexpected forEach ${key} lookup`);
      }});
      return callee;
    }});
    return owner;
  };
  const firstList = list('early'), secondList = list('late');
  root = new vm.SourceTextModule(source, {context});
  base = new vm.SyntheticModule(['CARD_LIST'], function() { this.setExport('CARD_LIST', firstList); }, {context});
  detail = new vm.SyntheticModule(['CardItem'], function() { this.setExport('CardItem', original); }, {context});
  await root.link(name => {
    assert.ok(name === '@normalized:N&&&base/Index&1.0.0' ||
      name === '@normalized:N&&&detail/src/main/ets/viewmodel/CardViewModel&1.0.0');
    return name.includes('CardViewModel') ? detail : base;
  });
  assert.throws(() => root.namespace.CardArray, {name: 'ReferenceError'});
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  let raised = false, thrown, initialized = false, singleton;
  const noncallable = mode === Mode.listNoncallable || mode === Mode.pushNoncallable || mode === Mode.itemNonconstructible;
  try {
    await root.evaluate(); initialized = true;
    assert.equal(noncallable, false);
    singleton = root.namespace.default;
    const C = root.namespace.CardArray;
    assert.equal(singleton.getInstance(), singleton); assert.equal(C.length, 0); assert.equal(C.name, 'CardArray');
    assert.throws(() => Reflect.apply(C, {}, []), {name: 'TypeError'});
    assert.throws(() => Reflect.construct(C.prototype.getInstance, []), {name: 'TypeError'});
    const borrowed = new Proxy({}, {get() { assert.fail('getInstance must not read properties'); }});
    assert.equal(Reflect.apply(C.prototype.getInstance, borrowed, [1]), borrowed);
    assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor', 'getInstance']);
    const instance = new C('ignored'); assert.notEqual(instance, singleton);
    const instances = nested ? [singleton, instance, nested] : [singleton, instance];
    for (const actual of instances) {
      assert.equal(Object.getPrototypeOf(actual), C.prototype);
      assert.deepEqual(Object.keys(actual), ['cardArray']);
      const field = Object.getOwnPropertyDescriptor(actual, 'cardArray');
      assert.deepEqual([field.writable, field.enumerable, field.configurable], [true, true, true]);
      assert.equal(Object.getPrototypeOf(actual.cardArray), arrayPrototype);
    }
    assert.notEqual(instance.cardArray, singleton.cardArray);
    // A retained original arrow reads the original receiver's current field,
    // after default initialization and a second/reentrant construction.
    singleton.cardArray = vm.runInContext('[]', context);
    Reflect.apply(callbacks.find(record => record.depth === 0).fn, {cardArray: 'wrong'}, [items[1], 1]);
    assert.equal(singleton.cardArray.length, 1);
    await root.evaluate(); assert.equal(root.namespace.default, singleton);
  } catch (error) { raised = true; thrown = error; }
  if (raised) {
    if (failureAt) assert.equal(thrown, failure);
    else { assert.equal(noncallable, true); assert.equal(thrown?.name, 'TypeError'); }
    if (!initialized) {
      assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
      await assert.rejects(root.evaluate(), error => error === thrown);
    } else assert.equal(root.namespace.default, singleton);
  }
  if (raised && failureAt && callbacks.length) {
    // A callback retained before an aborted construction still owns its real
    // receiver environment. Recover the host fault and invoke that closure;
    // this must neither restart nor repair the failed module evaluation.
    inject = false; depth = 2;
    Reflect.apply(callbacks[0].fn, {cardArray: 'wrong after failure'}, [items[1], 1]);
    if (!initialized) {
      assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
      await assert.rejects(root.evaluate(), error => error === thrown);
    }
  }
  return {trace, points: [...points], raised, initialized,
    arrays: arrays.map(array => Array.from(array, item => [item.label, item.index, item.item === items[item.index]])),
    retained: typeof callback === 'function'};
}

// The pinned bytecode initializes cardArray by assignment. Native TS field
// DefineField differs for inherited setters, so this layer checks the actual
// STOBJBYNAME obligations without treating Node's field syntax as that oracle.
async function assignedConstructor(source, prototypeKind, failureAt = null, failure) {
  const context = vm.createContext({}), trace = [], stored = new WeakMap();
  const item = {}, stop = {};
  let active = false, receiver, field, returned, raised = false, thrown;
  const event = (label, ...values) => { trace.push([label, ...values]); if (label === failureAt) throw failure; };
  function CardItem(value, index) {
    assert.equal(value, item); assert.equal(index, 0); assert.equal(new.target, CardItem);
    if (active) event('item'); this.item = value;
  }
  const list = {forEach(callback) {
    assert.equal(this, list);
    if (active) event('list');
    Reflect.apply(callback, {cardArray: stop}, [item, 0]);
  }};
  const root = new vm.SourceTextModule(source, {context});
  const base = new vm.SyntheticModule(['CARD_LIST'], function() { this.setExport('CARD_LIST', list); }, {context});
  const detail = new vm.SyntheticModule(['CardItem'], function() { this.setExport('CardItem', CardItem); }, {context});
  await root.link(name => name.includes('CardViewModel') ? detail : base); await root.evaluate();
  const C = root.namespace.CardArray;
  const targetContext = prototypeKind === PrototypeKind.foreignFallback ? vm.createContext({}) : context;
  const fallback = vm.runInContext('Object.prototype', targetContext);
  const usesFallback = prototypeKind === PrototypeKind.fallback || prototypeKind === PrototypeKind.foreignFallback;
  const prototype = usesFallback ? fallback : prototypeKind === PrototypeKind.class ? C.prototype : {};
  Object.defineProperty(prototype, 'cardArray', {configurable: true,
    set(value) {
      receiver = this; field = value; stored.set(this, value);
      assert.ok(Array.isArray(value)); assert.equal(value.length, 0);
      assert.equal(Object.getPrototypeOf(value), vm.runInContext('Array.prototype', context));
      event('set');
    },
    get() { assert.equal(this, receiver); event('get'); return stored.get(this); },
  });
  const Target = vm.runInContext('(function Target() {})', targetContext);
  const target = new Proxy(Target, {get(object, key, actual) {
    if (key === 'prototype') { event('prototype'); return usesFallback ? 7 : prototype; }
    return Reflect.get(object, key, actual);
  }});
  active = true;
  try { returned = Reflect.construct(C, ['ignored'], target); }
  catch (error) { raised = true; thrown = error; }
  if (failureAt) { assert.equal(raised, true); assert.equal(thrown, failure); }
  else {
    assert.equal(raised, false); assert.equal(returned, receiver);
    assert.equal(Object.getPrototypeOf(returned), prototype);
    assert.equal(Reflect.apply(C.prototype.getInstance, returned, []), returned);
    assert.deepEqual(Reflect.ownKeys(returned), []);
    assert.equal(field.length, 1); assert.equal(field[0].item, item);
  }
  const expected = ['prototype', 'set', 'list', 'get', 'item'];
  assert.deepEqual(trace.map(row => row[0]), failureAt ? expected.slice(0, expected.indexOf(failureAt) + 1) : expected);
  if (receiver) {
    assert.deepEqual(Reflect.ownKeys(receiver), []);
    assert.equal(field.length, failureAt ? 0 : 1);
  } else assert.equal(failureAt, 'prototype');
}

export async function verifyCardArrayState(source, reference) {
  const modes = [Mode.normal, Mode.reverse, Mode.listLive, Mode.callLive, Mode.itemLive, Mode.pushLive,
    Mode.pushMutate, Mode.reentrant, Mode.listNoncallable, Mode.pushNoncallable, Mode.itemNonconstructible];
  let scenarios = 0, throwCases = 0;
  for (const mode of modes) {
    const baseline = await observe(source, mode);
    assert.deepEqual(baseline, await observe(reference, mode)); ++scenarios;
    if (baseline.raised) continue;
    for (const point of baseline.points) for (const failure of [undefined, 0, Symbol('failure'), {}]) {
      assert.deepEqual(await observe(source, mode, point, failure), await observe(reference, mode, point, failure));
      ++throwCases;
    }
  }
  const baseline = await observe(source, Mode.itemLive);
  let mutations = 0;
  const arrow = /\(item, index\) =>/;
  for (const changed of [
    source.replace('new CardItem(item, index)', 'new CardItem(item, 0)'),
    source.replace(arrow, 'function(item, index)'),
    source.replace('.cardArray.push(new CardItem', '.cardArray.unshift(new CardItem'),
  ]) {
    assert.notEqual(changed, source); new vm.SourceTextModule(changed);
    await assert.rejects(observe(changed, Mode.itemLive).then(actual => assert.deepEqual(actual, baseline)), {name: 'AssertionError'});
    ++mutations;
  }
  let assignmentCases = 0;
  for (const kind of [PrototypeKind.class, PrototypeKind.foreign, PrototypeKind.fallback, PrototypeKind.foreignFallback]) {
    await assignedConstructor(source, kind); ++assignmentCases;
    for (const point of ['prototype', 'set', 'list', 'get', 'item'])
      for (const failure of [undefined, 0, Symbol('failure'), {}]) {
        await assignedConstructor(source, kind, point, failure); ++assignmentCases;
      }
  }
  for (const changed of [
    source.replace(/([\w$]+)\.cardArray = \[\];/,
      'Object.defineProperty($1, "cardArray", {value: [], writable: true, enumerable: true, configurable: true});'),
    source.replace(/return ([\w$]+);/, 'return ({});'),
  ]) {
    assert.notEqual(changed, source); new vm.SourceTextModule(changed);
    await assert.rejects(assignedConstructor(changed, PrototypeKind.foreign), {name: 'AssertionError'}); ++mutations;
  }
  return {scenarios, throwCases, mutations, assignmentCases};
}
