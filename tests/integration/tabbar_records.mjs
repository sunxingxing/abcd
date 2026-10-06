import assert from 'node:assert/strict';
import vm from 'node:vm';

const specifier = '@normalized:N&&&phone/src/main/ets/model/TabBarModel&';
const Prototype = Object.freeze({class: Symbol('class'), foreign: Symbol('foreign'),
  fallback: Symbol('fallback'), foreignFallback: Symbol('foreign-fallback')});

// Preserve the complete literal graph: ordering, descriptors, realm prototypes,
// cycles and shared/distinct identities. This oracle never copies a reference
// literal into the engine, nor substitutes a model value for emitted output.
function graph(value, objectPrototype, arrayPrototype) {
  const ids = new Map(), pending = [], result = [];
  const ref = node => {
    if (node === null || typeof node !== 'object') return {value: node};
    if (!ids.has(node)) { ids.set(node, pending.length); pending.push(node); }
    return {object: ids.get(node)};
  };
  ref(value);
  for (let i = 0; i < pending.length; ++i) {
    const node = pending[i], array = Array.isArray(node);
    assert.equal(Object.getPrototypeOf(node), array ? arrayPrototype : objectPrototype);
    result.push({array, extensible: Object.isExtensible(node), fields: Reflect.ownKeys(node).map(key => {
      const d = Object.getOwnPropertyDescriptor(node, key);
      assert.equal(d.get, undefined); assert.equal(d.set, undefined);
      return [key, d.writable, d.enumerable, d.configurable, ref(d.value)];
    })});
  }
  return result;
}

async function connected(viewSource, modelSource) {
  const context = vm.createContext({}), events = [];
  const objectPrototype = vm.runInContext('Object.prototype', context);
  const arrayPrototype = vm.runInContext('Array.prototype', context);
  // Buffer recovery must use object/array literal property definition. Native
  // literals bypass inherited setters and avoid replaceable global helpers.
  for (const key of ['name', 'icon', 'iconSelected', 'id', 'type', 'params', 'bundleName', 'moduleName'])
    Object.defineProperty(objectPrototype, key, {configurable: true, set() { events.push(key); assert.fail('literal inherited setter'); }});
  for (const key of ['0', '1', '2', '3']) Object.defineProperty(arrayPrototype, key, {
    configurable: true, set() { events.push(key); assert.fail('array inherited setter'); },
  });
  context.Object = undefined; context.Array = undefined;
  const model = new vm.SourceTextModule(modelSource, {context});
  const root = new vm.SourceTextModule(viewSource, {context});
  await root.link(name => { assert.equal(name, specifier); return model; });
  assert.throws(() => model.namespace.TabBarsInfo, {name: 'ReferenceError'});
  assert.throws(() => root.namespace.TabBarViewModel, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.keys(model.namespace), ['TabBarsInfo']);
  assert.deepEqual(Object.keys(root.namespace), ['TabBarViewModel']);
  const list = model.namespace.TabBarsInfo, C = root.namespace.TabBarViewModel;
  assert.equal(C.name, 'TabBarViewModel'); assert.equal(C.length, 0);
  assert.equal(C.prototype.getTabList.name, 'getTabList'); assert.equal(C.prototype.getTabList.length, 0);
  assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor', 'getTabList']);
  const method = Object.getOwnPropertyDescriptor(C.prototype, 'getTabList');
  assert.deepEqual([method.writable, method.enumerable, method.configurable], [true, false, true]);
  assert.throws(() => Reflect.apply(C, {}, []), {name: 'TypeError'});
  assert.throws(() => Reflect.construct(method.value, []), {name: 'TypeError'});
  const first = new C('ignored'), second = new C();
  assert.notEqual(first, second);
  for (const instance of [first, second]) {
    assert.equal(Object.getPrototypeOf(instance), C.prototype);
    assert.deepEqual(Reflect.ownKeys(instance), ['tabList']);
    assert.deepEqual(Object.getOwnPropertyDescriptor(instance, 'tabList'), {
      value: list, writable: true, enumerable: true, configurable: true,
    });
    assert.equal(instance.getTabList(), list);
  }
  const before = graph(list, objectPrototype, arrayPrototype);
  // Both existing instances hold the same live object, but a field replacement
  // belongs to one instance and must not update the exported module binding.
  const marker = {};
  Object.defineProperty(list[0].name.params, '0', {value: marker, writable: true, enumerable: true, configurable: true});
  assert.equal(second.getTabList()[0].name.params[0], marker);
  assert.equal(list[0].icon.params.length, 0); // Distinct resource params arrays.
  first.tabList = marker;
  assert.equal(first.getTabList(), marker); assert.equal(second.getTabList(), list);
  const receiver = new Proxy({}, {get(_, key, actual) {
    assert.equal(actual, receiver); assert.equal(key, 'tabList'); events.push('borrowed:get'); return marker;
  }});
  assert.equal(Reflect.apply(method.value, receiver, ['ignored']), marker);
  assert.deepEqual(events, ['borrowed:get']);
  for (const bad of [null, undefined]) assert.throws(() => Reflect.apply(method.value, bad, []), {name: 'TypeError'});
  for (const primitive of [false, 0, NaN, 'text', Symbol('receiver'), 1n])
    assert.equal(Reflect.apply(method.value, primitive, []), undefined);
  assert.equal(Reflect.set(model.namespace, 'TabBarsInfo', []), false);
  assert.equal(Reflect.deleteProperty(model.namespace, 'TabBarsInfo'), false);
  await root.evaluate();
  assert.equal(model.namespace.TabBarsInfo, list); assert.equal(root.namespace.TabBarViewModel, C);
  assert.equal(new C().getTabList(), list);
  return {before, events};
}

async function imported(viewSource, initial) {
  const context = vm.createContext({});
  const dependency = new vm.SyntheticModule(['TabBarsInfo'], function() {
    this.setExport('TabBarsInfo', initial);
  }, {context});
  const root = new vm.SourceTextModule(viewSource, {context});
  await root.link(name => { assert.equal(name, specifier); return dependency; });
  assert.throws(() => root.namespace.TabBarViewModel, {name: 'ReferenceError'});
  await root.evaluate();
  return {context, C: root.namespace.TabBarViewModel, dependency};
}

async function live(viewSource, initial, next) {
  const {C, dependency} = await imported(viewSource, initial);
  const first = new C(); assert.equal(Object.getPrototypeOf(first), C.prototype);
  assert.equal(first.getTabList(), initial);
  dependency.setExport('TabBarsInfo', next);
  assert.equal(first.getTabList(), initial);
  const second = new C(); assert.equal(Object.getPrototypeOf(second), C.prototype);
  assert.notEqual(first, second); assert.equal(second.getTabList(), next);
  const trace = [], failure = {};
  Object.defineProperty(second, 'tabList', {configurable: true, get() {
    assert.equal(this, second); trace.push('field');
    dependency.setExport('TabBarsInfo', initial); throw failure;
  }});
  assert.throws(() => second.getTabList(), error => error === failure);
  assert.deepEqual(trace, ['field']);
  assert.equal(new C().getTabList(), initial);
}

async function assignment(viewSource, kind, fault = null, failure) {
  const initial = {}, replacement = {}, trace = [];
  const {context, C, dependency} = await imported(viewSource, initial);
  const targetContext = kind === Prototype.foreignFallback ? vm.createContext({}) : context;
  const fallback = kind === Prototype.fallback || kind === Prototype.foreignFallback;
  const prototype = fallback ? vm.runInContext('Object.prototype', targetContext) : kind === Prototype.class ? C.prototype : {};
  let receiver, stored, returned, raised = false, thrown;
  const event = label => { trace.push(label); if (label === fault) throw failure; };
  Object.defineProperty(prototype, 'tabList', {configurable: true,
    set(value) {
      receiver = this; assert.equal(value, initial); event('set'); stored = value;
      dependency.setExport('TabBarsInfo', replacement);
    },
    get() { assert.equal(this, receiver); event('get'); return stored; },
  });
  const Target = vm.runInContext('(function Target() {})', targetContext);
  const target = new Proxy(Target, {get(object, key, actual) {
    if (key === 'prototype') { event('prototype'); return fallback ? 7 : prototype; }
    return Reflect.get(object, key, actual);
  }});
  try {
    returned = Reflect.construct(C, ['ignored'], target);
    assert.equal(returned, receiver); assert.equal(Object.getPrototypeOf(returned), prototype);
    assert.deepEqual(Reflect.ownKeys(returned), []);
    assert.equal(Reflect.apply(C.prototype.getTabList, returned, []), initial);
  } catch (error) { raised = true; thrown = error; }
  const expected = ['prototype', 'set', 'get'];
  assert.deepEqual(trace, fault ? expected.slice(0, expected.indexOf(fault) + 1) : expected);
  assert.equal(raised, fault !== null);
  if (fault) assert.equal(thrown, failure);
  if (receiver) {
    assert.deepEqual(Reflect.ownKeys(receiver), []);
    assert.equal(stored, fault === 'set' ? undefined : initial);
  } else assert.equal(fault, 'prototype');
}

export async function verifyTabBarRecords(view, model, referenceView, referenceModel) {
  assert.match(view, /constructor\(\)/); assert.match(view, /this\.tabList = TabBarsInfo/);
  assert.match(view, /getTabList\(\)\s*\{\s*return this\.tabList;/);
  assert.match(model, /\b(?:let|const) TabBarsInfo = \[/);
  assert.match(model, /export \{ TabBarsInfo as "TabBarsInfo" \}/);
  for (const source of [view, model]) assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED|\$value|\$acc|Reflect\.construct|\.call\(|\bswitch\s*\(/);
  const expected = await connected(referenceView, referenceModel);
  assert.deepEqual(await connected(view, model), expected);
  // Independent activations/realms must allocate distinct model literal graphs.
  assert.deepEqual(await connected(view, model), expected);
  const symbol = Symbol('value'), hostile = {[Symbol.toPrimitive]() { assert.fail('no import coercion'); }};
  const values = [undefined, null, false, true, 0, -0, NaN, Infinity, '', 'text', 1n, symbol, {}, hostile];
  let tabBarLiveCases = 0, tabBarAssignments = 0, tabBarMutations = 0;
  for (const initial of values) for (const next of values) {
    await live(view, initial, next); await live(referenceView, initial, next); ++tabBarLiveCases;
  }
  for (const kind of Object.values(Prototype)) {
    // The original initialized TS field uses DefineField in Node. Its setter
    // behavior is not an oracle for pinned Ark STOBJBYNAME assignment.
    await assignment(view, kind); ++tabBarAssignments;
    for (const fault of ['prototype', 'set', 'get']) for (const failure of [undefined, 0, Symbol('failure'), {}]) {
      await assignment(view, kind, fault, failure); ++tabBarAssignments;
    }
  }
  for (const changed of [view.replace('this.tabList = TabBarsInfo', 'this.tabList = []'),
    view.replace('return this.tabList;', 'return TabBarsInfo;'),
    view.replace('(this.tabList = TabBarsInfo);', 'return {tabList: TabBarsInfo};'),
    view.replace('(this.tabList = TabBarsInfo);', 'Object.defineProperty(this, "tabList", {value: TabBarsInfo, configurable: true, enumerable: true, writable: true});'),
  ]) {
    assert.notEqual(changed, view); new vm.SourceTextModule(changed);
    await assert.rejects((async () => {
      const token = {}; await live(changed, token, Symbol('replacement'));
      await assignment(changed, Prototype.foreign);
    })(), {name: 'AssertionError'}); ++tabBarMutations;
  }
  for (const changed of [model.replace(/id: (\d+)/, (_, id) => `id: ${Number(id) + 1}`),
    `const shared = [];\n${model.replaceAll('params: []', 'params: shared')}`]) {
    assert.notEqual(changed, model); new vm.SourceTextModule(changed);
    await assert.rejects(connected(view, changed).then(actual => assert.deepEqual(actual, expected)),
      {name: 'AssertionError'}); ++tabBarMutations;
  }
  return {tabBarConnectedCases: 2, tabBarLiteralNodes: expected.before.length,
    tabBarLiveCases, tabBarAssignments, tabBarMutations,
    tabBarModelStyle: 'accepted', tabBarViewModelStyle: 'accepted'};
}
