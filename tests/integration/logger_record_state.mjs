import assert from 'node:assert/strict';
import vm from 'node:vm';

const methods = ['debug', 'info', 'warn', 'error'];
const fields = ['format', 'prefix', 'domain'];
const format = '%{public}s, %{public}s';
const domain = 65280;
async function load(source, hilog) {
  const context = vm.createContext({});
  const dependency = new vm.SyntheticModule(['default'], function() {
    this.setExport('default', hilog);
  }, {context});
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => { assert.equal(name, '@ohos:hilog'); return dependency; });
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  await root.evaluate();
  const instance = root.namespace.default, C = instance.constructor;
  assert.equal(C.name, 'Logger'); assert.equal(C.length, 1);
  assert.equal(Object.getPrototypeOf(instance), C.prototype);
  assert.equal(Object.getPrototypeOf(C), vm.runInContext('Function.prototype', context));
  assert.equal(Object.getPrototypeOf(C.prototype), vm.runInContext('Object.prototype', context));
  assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor', ...methods]);
  assert.deepEqual(Object.keys(root.namespace), ['default']);
  await root.evaluate(); assert.equal(root.namespace.default, instance);
  return {root, context, dependency, instance, C};
}

// Real native rest arrays, imported callee acquisition and receiver fields are
// independently observed. A reentrant call must own another rest allocation.
async function invocation(source, method, args, mode, throwAt = null, failure) {
  const trace = [], points = new Set(), arrays = [], state = {domain, prefix: 'tag', format};
  let dependency, loaded, receiver, depth = 0, raised = false, result, thrown;
  function event(name, ...values) {
    points.add(name); trace.push([name, ...values]);
    if (name === throwAt) throw failure;
  }
  function host(label) {
    const owner = {};
    for (const name of methods) Object.defineProperty(owner, name, {get() {
      const callDepth = depth; event(`${depth}:${label}.${name}:get`);
      if (mode === 'callee-live') dependency.setExport('default', replacement);
      if (mode === 'callee-mutate') { state.domain = 17; state.prefix = 'changed'; state.format = 'new format'; }
      if (mode === 'reentrant' && depth === 0) {
        ++depth;
        try { assert.equal(Reflect.apply(loaded.C.prototype.warn, receiver, args), undefined); }
        finally { --depth; }
      }
      if (mode === 'noncallable') return null;
      const callee = function(...incoming) {
        assert.equal(this, owner); assert.equal(incoming.length, 4);
        const [actualDomain, actualPrefix, actualFormat, rest] = incoming;
        event(`${callDepth}:${label}.${name}:call`, actualDomain, actualPrefix, actualFormat, rest.length);
        assert.ok(Array.isArray(rest));
        assert.equal(Object.getPrototypeOf(rest), vm.runInContext('Array.prototype', loaded.context));
        assert.notEqual(rest, args); assert.ok(arrays.every(previous => previous !== rest));
        assert.equal(rest.length, args.length);
        assert.deepEqual(Object.keys(rest), Array.from({length: args.length}, (_, i) => String(i)));
        for (let i = 0; i < args.length; ++i) {
          const descriptor = Object.getOwnPropertyDescriptor(rest, String(i));
          assert.ok(Object.is(descriptor.value, args[i]));
          assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
        }
        arrays.push(rest); return 'discard this host return';
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callee, key, {get() {
        assert.fail(`ambient ${key} lookup`);
      }});
      return callee;
    }});
    return owner;
  }
  const original = host('old'), replacement = host('new');
  loaded = await load(source, original); dependency = loaded.dependency;
  if (mode === 'before-live') dependency.setExport('default', replacement);
  receiver = new Proxy({}, {get(target, key, actualReceiver) {
    assert.equal(actualReceiver, receiver); assert.ok(['domain', 'prefix', 'format'].includes(key));
    event(`${depth}:field:${key}`);
    if (mode === `${key}-live`) dependency.setExport('default', replacement);
    if (mode === 'domain-mutate' && key === 'domain') { state.prefix = 'late tag'; state.format = 'late format'; }
    return state[key];
  }});
  const descriptor = Object.getOwnPropertyDescriptor(loaded.C.prototype, method);
  assert.equal(descriptor.value.name, method); assert.equal(descriptor.value.length, 0);
  assert.deepEqual([descriptor.enumerable, descriptor.writable, descriptor.configurable], [false, true, true]);
  assert.throws(() => Reflect.construct(descriptor.value, []), {name: 'TypeError'});
  try { result = Reflect.apply(descriptor.value, receiver, args); }
  catch (error) { raised = true; thrown = error; }
  if (throwAt) { assert.equal(raised, true); assert.equal(thrown, failure); }
  else if (mode === 'noncallable') { assert.equal(raised, true); assert.equal(thrown?.name, 'TypeError'); }
  else { assert.equal(raised, false); assert.equal(result, undefined); }
  return {trace, points: [...points], raised, thrown: raised ? (throwAt ? 'original' : 'TypeError') : null,
    restCount: arrays.length, state};
}

async function constructors(source, prefix, values) {
  const {instance, C} = await load(source, {});
  assert.equal(instance.prefix, prefix); assert.equal(instance.domain, domain); assert.equal(instance.format, format);
  assert.throws(() => C(), {name: 'TypeError'});
  for (const value of values) {
    const first = new C(value, 'ignored', {}), second = new C(value);
    assert.notEqual(first, second); assert.notEqual(first, instance);
    assert.equal(Object.getPrototypeOf(first), C.prototype);
    assert.deepEqual(Object.keys(first), fields);
    for (const [key, expected] of [['format', format], ['prefix', value], ['domain', domain]]) {
      const descriptor = Object.getOwnPropertyDescriptor(first, key);
      assert.ok(Object.is(descriptor.value, expected));
      assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
    }
  }
}

// Pinned constructor writes invoke inherited setters. Node's initialized TS
// format field uses DefineField, so this layer is intentionally not paired.
async function assignments(source, prefix, alternate, failAt, failure) {
  const {C} = await load(source, {}), trace = [], stored = new WeakMap();
  let receiver;
  const prototype = alternate ? {} : C.prototype;
  for (const key of fields) Object.defineProperty(prototype, key, {set(value) {
    trace.push([key, value]); receiver = this;
    let state = stored.get(this); if (!state) stored.set(this, state = {});
    state[key] = value; if (key === failAt) throw failure;
  }});
  function Target() {}
  const target = new Proxy(Target, {get(object, key, actualReceiver) {
    if (key === 'prototype') {
      trace.push(['prototype']); if (failAt === 'prototype') throw failure; return prototype;
    }
    return Reflect.get(object, key, actualReceiver);
  }});
  const invoke = () => Reflect.construct(C, [prefix], alternate ? target : C);
  let raised = false, instance;
  try { instance = invoke(); }
  catch (error) { raised = true; assert.equal(error, failure); }
  assert.equal(raised, Boolean(failAt));
  const written = failAt === 'prototype' ? [] : failAt ? fields.slice(0, fields.indexOf(failAt) + 1) : fields;
  assert.deepEqual(trace, [...(alternate ? [['prototype']] : []),
    ...written.map(key => [key, key === 'format' ? format : key === 'prefix' ? prefix : domain])]);
  if (failAt === 'prototype') assert.equal(receiver, undefined);
  else { assert.ok(receiver); assert.deepEqual(Object.keys(receiver), []); }
  if (instance) {
    assert.equal(receiver, instance); assert.equal(Object.getPrototypeOf(instance), prototype);
    assert.deepEqual(stored.get(instance), {format, prefix, domain});
  }
}

async function initialization(source, prefix, failAt, failure) {
  const trace = [], stored = new WeakMap();
  let receiver;
  const context = vm.createContext({recordField(object, key, value) {
    receiver = object; trace.push([key, value]);
    let state = stored.get(object); if (!state) stored.set(object, state = {});
    state[key] = value; if (key === failAt) throw failure;
  }});
  const root = new vm.SourceTextModule(source, {context});
  const dependency = new vm.SyntheticModule(['default'], function() { this.setExport('default', {}); }, {context});
  await root.link(name => { assert.equal(name, '@ohos:hilog'); return dependency; });
  vm.runInContext(`for (const key of ['format', 'prefix', 'domain']) {
    Object.defineProperty(Object.prototype, key, {configurable: true,
      set(value) { recordField(this, key, value); }});
  }`, context);
  let raised = false;
  try { await root.evaluate(); }
  catch (error) { raised = true; assert.equal(error, failure); }
  assert.equal(raised, Boolean(failAt));
  const written = failAt ? fields.slice(0, fields.indexOf(failAt) + 1) : fields;
  assert.deepEqual(trace, written.map(key => [key, key === 'format' ? format : key === 'prefix' ? prefix : domain]));
  assert.ok(receiver); assert.equal(receiver.constructor.name, 'Logger');
  assert.deepEqual(Object.keys(receiver), []);
  if (raised) {
    assert.equal(root.status, 'errored');
    assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
    let repeated = false;
    try { await root.evaluate(); } catch (error) { repeated = true; assert.equal(error, failure); }
    assert.equal(repeated, true); assert.equal(trace.length, written.length);
  } else {
    assert.equal(root.namespace.default, receiver); assert.equal(root.status, 'evaluated');
    await root.evaluate(); assert.equal(trace.length, 3); assert.equal(root.namespace.default, receiver);
  }
}

export async function verifyLoggerState(source, reference, prefix) {
  const values = [undefined, null, false, true, 0, -0, NaN, Infinity, 3n, 'tag', Symbol('tag'), {}, [],
    {get [Symbol.toPrimitive]() { assert.fail('constructor must not coerce prefix'); }}];
  const args = [[], [undefined], [undefined, null, -0, NaN, Symbol('arg'), 4n, {}, () => {}],
    Array.from({length: 200}, (_, i) => Object.freeze({i})),
    Array.from({length: 201}, (_, i) => Object.freeze({i}))];
  delete args[3][1];
  let pairedCases = 0, assignmentCases = 0, initializationCases = 0;
  const modes = ['normal', 'before-live', 'callee-live', 'domain-live', 'prefix-live', 'format-live',
    'callee-mutate', 'domain-mutate', 'reentrant', 'noncallable'];
  for (const method of methods) for (const input of args) for (const mode of modes) {
    const expected = await invocation(reference, method, input, mode);
    assert.deepEqual(await invocation(source, method, input, mode), expected); ++pairedCases;
    // Small independent matrices exercise every observed throw boundary;
    // large rest arrays test identity/shape without multiplying all probes.
    if (input === args[2]) for (const point of expected.points) for (const failure of [undefined, 0, {}]) {
      assert.deepEqual(await invocation(source, method, input, mode, point, failure),
        await invocation(reference, method, input, mode, point, failure)); ++pairedCases;
    }
  }
  for (const text of [source, reference]) await constructors(text, prefix, values);
  for (const alternate of [false, true]) for (const failAt of [null, ...fields, ...(alternate ? ['prototype'] : [])]) for (const failure of [undefined, 0, {}]) {
    await assignments(source, values[11], alternate, failAt, failure); ++assignmentCases;
  }
  for (const failAt of [null, ...fields]) for (const failure of [undefined, 0, {}]) {
    await initialization(source, prefix, failAt, failure); ++initializationCases;
  }
  const mutations = [
    {name: 'rest elements lost', change: text => text.replace(/(hilog\.debug\(this\.domain, this\.prefix, this\.format, )[^)]+/, '$1[]'),
     check: text => invocation(text, 'debug', args[2], 'normal')},
    {name: 'rest array shared across activations', change: text => 'let sharedRest;\n' +
      text.replace(/(hilog\.(?:debug|info|warn|error)\(this\.domain, this\.prefix, this\.format, )([^);]+)(\);)/g,
        '$1(sharedRest ??= $2)$3'),
     check: text => invocation(text, 'info', args[2], 'reentrant')},
    {name: 'host return leaked', change: text => text.replace('hilog.debug(', 'return hilog.debug('),
     check: text => invocation(text, 'debug', args[2], 'normal')},
    {name: 'callee receiver lost', change: text => text.replace('hilog.info(', '(0, hilog.info)('),
     check: text => invocation(text, 'info', args[2], 'normal')},
    {name: 'format setter bypassed', change: text => text.replace('this.format = "%{public}s, %{public}s"',
      'Object.defineProperty(this, "format", {value: "%{public}s, %{public}s", writable: true, enumerable: true, configurable: true})'),
     check: text => assignments(text, values[11], false, null, {})},
    {name: 'default prefix identity lost', change: text => text.replace('this.prefix = prefix', 'this.prefix = null'),
     check: text => initialization(text, prefix, null, {})},
  ];
  for (const {name, change, check} of mutations) {
    const changed = change(source); assert.notEqual(changed, source); new vm.SourceTextModule(changed);
    let detected = false;
    try { assert.deepEqual(await check(changed), await check(source)); }
    catch (error) { if (!(error instanceof assert.AssertionError)) throw error; detected = true; }
    assert.equal(detected, true, name);
  }
  return {pairedCases, constructorValues: values.length, assignmentCases, initializationCases, mutations: mutations.length};
}
