import assert from 'node:assert/strict';
import vm from 'node:vm';

const PrototypeKind = Object.freeze({
  class: Symbol('class'),
  foreign: Symbol('foreign'),
  fallback: Symbol('fallback'),
  foreignFallback: Symbol('foreign-fallback'),
});

const keys = ['sm', 'md', 'lg'];
async function construct(source, prototypeKind, setters, values, failureAt = null, failure) {
  const context = vm.createContext({}), trace = [], saved = new Map();
  const root = new vm.SourceTextModule(source, {context});
  const dependency = new vm.SyntheticModule(['BreakpointConstants'], function() {
    this.setExport('BreakpointConstants', {BREAKPOINT_MD: 'md', BREAKPOINT_LG: 'lg'});
  }, {context});
  await root.link(name => {
    assert.equal(name, '@normalized:N&&&base/src/main/ets/constants/BreakpointConstants&1.0.0');
    return dependency;
  });
  await root.evaluate();
  const C = root.namespace.BreakpointType;
  assert.equal(C.name, 'BreakpointType'); assert.equal(C.length, 3);
  assert.throws(() => Reflect.apply(C, {}, values), {name: 'TypeError'});
  const targetContext = prototypeKind === PrototypeKind.foreignFallback ? vm.createContext({}) : context;
  const fallback = prototypeKind === PrototypeKind.fallback || prototypeKind === PrototypeKind.foreignFallback;
  const prototype = fallback ? vm.runInContext('Object.prototype', targetContext) :
    prototypeKind === PrototypeKind.class ? C.prototype : {};
  let receiver, result, raised = false, thrown;
  const event = label => { trace.push(label); if (label === failureAt) throw failure; };
  if (setters) for (const key of keys) Object.defineProperty(prototype, key, {configurable: true,
    set(value) {
      if (receiver) assert.equal(this, receiver); receiver = this;
      assert.equal(value, values[keys.indexOf(key)]); saved.set(key, value); event(key);
    },
  });
  const Target = vm.runInContext('(function Target() {})', targetContext);
  const target = new Proxy(Target, {get(object, key, actual) {
    if (key === 'prototype') { event('prototype'); return fallback ? null : prototype; }
    return Reflect.get(object, key, actual);
  }});
  try { result = Reflect.construct(C, [...values, {ignored: true}], target); }
  catch (error) { raised = true; thrown = error; }
  if (failureAt) { assert.equal(raised, true); assert.equal(thrown, failure); }
  else {
    assert.equal(raised, false); assert.equal(Object.getPrototypeOf(result), prototype);
    if (setters) { assert.equal(result, receiver); assert.deepEqual(Reflect.ownKeys(result), []); }
    else {
      assert.deepEqual(Reflect.ownKeys(result), keys);
      for (let i = 0; i < keys.length; ++i) {
        const field = Object.getOwnPropertyDescriptor(result, keys[i]);
        assert.ok(Object.is(field.value, values[i]));
        assert.deepEqual([field.writable, field.enumerable, field.configurable], [true, true, true]);
      }
      for (const [key, index] of [['md', 1], ['lg', 2], ['other', 0]])
        assert.equal(Reflect.apply(C.prototype.getValue, result, [key]), values[index]);
    }
    assert.throws(() => Reflect.construct(C.prototype.getValue, []), {name: 'TypeError'});
  }
  const expected = setters ? ['prototype', ...keys] : ['prototype'];
  assert.deepEqual(trace, failureAt ? expected.slice(0, expected.indexOf(failureAt) + 1) : expected);
  if (setters && failureAt === 'prototype') assert.equal(receiver, undefined);
  const states = keys.filter(key => saved.has(key));
  if (setters) {
    const count = failureAt === 'prototype' ? 0 : failureAt ? keys.indexOf(failureAt) + 1 : keys.length;
    assert.deepEqual(states, keys.slice(0, count));
    if (receiver) assert.deepEqual(Reflect.ownKeys(receiver), []);
  }
  return {trace, raised, states};
}

export async function verifyBreakpointState(source, reference) {
  const prohibited = {[Symbol.toPrimitive]() { assert.fail('constructor must retain values without coercion'); }};
  const argumentsList = [[undefined, null, false], [-0, NaN, Infinity], [Symbol('sm'), {}, prohibited]];
  let constructorCases = 0, throwCases = 0;
  for (const kind of [PrototypeKind.class, PrototypeKind.foreign, PrototypeKind.fallback, PrototypeKind.foreignFallback])
    for (const setters of [false, true]) for (const values of argumentsList) {
      assert.deepEqual(await construct(source, kind, setters, values), await construct(reference, kind, setters, values));
      ++constructorCases;
      for (const point of setters ? ['prototype', ...keys] : ['prototype'])
        for (const failure of [undefined, 0, Symbol('failure'), {}]) {
          assert.deepEqual(await construct(source, kind, setters, values, point, failure),
            await construct(reference, kind, setters, values, point, failure));
          ++throwCases;
        }
    }
  let mutations = 0;
  for (const changed of [
    source.replace('(this.md = md);', '(this.md = lg);'),
    source.replace('(this.sm = sm);', '(this.sm = sm); return ({});'),
    source.replace('(this.sm = sm);', 'Object.defineProperty(this, "sm", {value: sm, writable: true, enumerable: true, configurable: true});'),
  ]) {
    assert.notEqual(changed, source); new vm.SourceTextModule(changed);
    await assert.rejects(construct(changed, PrototypeKind.foreign, true, argumentsList[2]), {name: 'AssertionError'});
    ++mutations;
  }
  return {constructorCases, throwCases, constructorMutations: mutations};
}
