import assert from 'node:assert/strict';
import vm from 'node:vm';

// This is an independent whole-ESM oracle. Snapshot the complete graph through
// own descriptors, including sharing, holes, realm prototypes and extensibility.
// No JSON normalization, sidecar input to the engine or replacement body.
function literalGraph(root, realm) {
  const objects = [], ids = new Map(), nodes = [];
  const ref = value => {
    if (value === null || typeof value !== 'object') return {value};
    if (!ids.has(value)) { ids.set(value, objects.length); objects.push(value); }
    return {object: ids.get(value)};
  };
  ref(root);
  for (let i = 0; i < objects.length; ++i) {
    const object = objects[i], array = Array.isArray(object);
    assert.equal(Object.getPrototypeOf(object), array ? realm.array : realm.object);
    nodes.push({array, extensible: Object.isExtensible(object), fields: Reflect.ownKeys(object).map(key => {
      const descriptor = Object.getOwnPropertyDescriptor(object, key);
      assert.equal(descriptor.get, undefined); assert.equal(descriptor.set, undefined);
      return [key, descriptor.writable, descriptor.enumerable, descriptor.configurable, ref(descriptor.value)];
    })});
  }
  return {nodes, objects};
}

function property(object, key) {
  const d = Object.getOwnPropertyDescriptor(object, key);
  assert.ok(d); assert.equal(d.get, undefined); assert.equal(d.set, undefined);
  return [d.writable, d.enumerable, d.configurable];
}

async function load(source, name, classRecord, hostile) {
  const context = vm.createContext({});
  const realm = vm.runInContext('({object: Object.prototype, array: Array.prototype, function: Function.prototype})', context);
  if (hostile) {
    const keys = classRecord ? ['FULL_PERCENT', 'FONT_WEIGHT_500', 'TEXT_OPACITY'] :
      ['title', 'name', 'children', 'to', 'id', 'type', 'params', 'bundleName', 'moduleName'];
    for (const key of keys) Object.defineProperty(classRecord ? realm.function : realm.object, key,
      {configurable: true, set() { assert.fail(`literal/static definition invoked inherited setter: ${key}`); }});
    for (const key of ['0', '1', '2', '3']) Object.defineProperty(realm.array, key,
      {configurable: true, set() { assert.fail(`array definition invoked inherited setter: ${key}`); }});
    // Native definitions must not depend on replaceable constructor/helpers.
    for (const key of ['Object', 'Array', 'Function', 'Reflect']) context[key] = undefined;
  }
  const module = new vm.SourceTextModule(source, {context});
  const consumer = new vm.SourceTextModule(`import {${name}} from 'constants';
    export function read() { return ${name}; }`, {context});
  await consumer.link(specifier => { assert.equal(specifier, 'constants'); return module; });
  assert.equal(module.status, 'linked');
  assert.throws(() => module.namespace[name], {name: 'ReferenceError'});
  await consumer.evaluate();
  assert.equal(module.status, 'evaluated');
  assert.deepEqual(Object.keys(module.namespace), [name]);
  assert.equal(Object.getPrototypeOf(module.namespace), null);
  assert.equal(Object.isExtensible(module.namespace), false);
  assert.deepEqual(property(module.namespace, name), [true, true, false]);
  assert.equal(Reflect.set(module.namespace, name, {}), false);
  assert.equal(Reflect.deleteProperty(module.namespace, name), false);
  assert.equal(consumer.namespace.read(), module.namespace[name]);
  const sibling = new vm.SourceTextModule(source, {context});
  await sibling.link(() => assert.fail('unexpected constants import'));
  await sibling.evaluate();
  return {context, realm, module, consumer, sibling, value: module.namespace[name]};
}

function disjoint(first, second) {
  const identities = new Set(first);
  for (const object of second) assert.equal(identities.has(object), false, 'fresh module reused literal identity');
}

async function routes(source, hostile, initialGraph) {
  const state = await load(source, 'ROUTES', false, hostile);
  const {realm, module, consumer, sibling, value} = state;
  const before = literalGraph(value, realm), fresh = literalGraph(sibling.namespace.ROUTES, realm);
  if (initialGraph) assert.deepEqual(before.nodes, initialGraph);
  assert.deepEqual(before.nodes, fresh.nodes); disjoint(before.objects, fresh.objects);
  // Consumer reads and re-evaluation retain the same mutable graph. Mutating one
  // params array must not affect another, even when both initially look empty.
  const params = value[0].title.params, otherParams = value[0].children[0].title.params;
  assert.notEqual(params, otherParams);
  Object.defineProperty(params, '0', {value: undefined, writable: true, enumerable: true, configurable: true});
  Object.defineProperty(params, '2', {value: -0, writable: true, enumerable: true, configurable: true});
  assert.equal(otherParams.length, 0);
  value[0].children[0].to = 'changed';
  delete value[1].children[0].name;
  Object.preventExtensions(value[1].title);
  const after = literalGraph(value, realm).nodes;
  await module.evaluate(); await consumer.evaluate();
  assert.equal(consumer.namespace.read(), value);
  assert.deepEqual(literalGraph(value, realm).nodes, after);
  assert.deepEqual(literalGraph(sibling.namespace.ROUTES, realm).nodes, before.nodes);
  return {before: before.nodes, after, nodes: before.objects.length};
}

async function staticClass(source, hostile) {
  const state = await load(source, 'CommonConstants', true, hostile);
  const {context, realm, module, consumer, sibling, value: C} = state;
  assert.equal(C.name, 'CommonConstants'); assert.equal(C.length, 0);
  assert.equal(Object.getPrototypeOf(C), realm.function);
  assert.equal(Object.getPrototypeOf(C.prototype), realm.object);
  assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor']);
  assert.equal(C.prototype.constructor, C);
  assert.deepEqual(property(C.prototype, 'constructor'), [true, false, true]);
  assert.throws(() => Reflect.apply(C, {}, []), {name: 'TypeError'});
  const fields = Reflect.ownKeys(C).map(key => [key, ...property(C, key),
    key === 'prototype' ? '<prototype>' : key === 'TEXT_OPACITY' ? literalGraph(C[key], realm).nodes : C[key]]);
  disjoint(literalGraph(C.TEXT_OPACITY, realm).objects,
    literalGraph(sibling.namespace.CommonConstants.TEXT_OPACITY, realm).objects);
  const instances = [];
  for (const count of [0, 1, 2, 3, 4, 6, 200, 201]) {
    const token = new Proxy({}, {get() { assert.fail('empty constructor inspected an argument'); }});
    const instance = Reflect.construct(C, Array(count).fill(token));
    assert.equal(Object.getPrototypeOf(instance), C.prototype);
    assert.deepEqual(Reflect.ownKeys(instance), []); instances.push(instance);
  }
  assert.equal(new Set(instances).size, instances.length);
  C.TEXT_OPACITY[0] = -0; C.TEXT_OPACITY[2] = NaN;
  const changed = literalGraph(C.TEXT_OPACITY, realm).nodes;
  assert.deepEqual(literalGraph(sibling.namespace.CommonConstants.TEXT_OPACITY, realm).nodes,
    fields.find(field => field[0] === 'TEXT_OPACITY').at(-1));
  C.FULL_PERCENT = undefined;
  const replacement = vm.runInContext('({replacement: true})', context);
  C.TEXT_OPACITY = replacement;
  assert.equal(consumer.namespace.read().TEXT_OPACITY, replacement);
  assert.equal(Reflect.deleteProperty(C, 'FONT_WEIGHT_500'), true);
  await module.evaluate(); await consumer.evaluate();
  assert.equal(consumer.namespace.read(), C);
  assert.equal(C.FULL_PERCENT, undefined); assert.equal(C.TEXT_OPACITY, replacement);
  assert.equal(Object.hasOwn(C, 'FONT_WEIGHT_500'), false);
  return {fields, changed, constructors: instances.length};
}

async function construction(source, kind, fault, failure) {
  const {context, realm, value: C} = await load(source, 'CommonConstants', true, false);
  const targetContext = kind === 'foreign' || kind === 'foreign-fallback' ? vm.createContext({}) : context;
  const Target = vm.runInContext('(function Target() { throw "must not call new.target"; })', targetContext);
  const objectPrototype = vm.runInContext('Object.prototype', targetContext);
  const prototype = kind.endsWith('fallback') ? 0 : kind === 'class' ? C.prototype : Object.create(objectPrototype);
  let calls = 0;
  const target = new Proxy(Target, {get(object, key, receiver) {
    assert.equal(key, 'prototype'); assert.equal(receiver, target); ++calls;
    if (fault) throw failure;
    return prototype;
  }});
  if (fault) assert.throws(() => Reflect.construct(C, [Symbol('ignored')], target), error => Object.is(error, failure));
  else {
    const instance = Reflect.construct(C, [Symbol('ignored')], target);
    assert.equal(Object.getPrototypeOf(instance), typeof prototype === 'object' ? prototype : objectPrototype);
    assert.deepEqual(Reflect.ownKeys(instance), []);
  }
  assert.equal(calls, 1);
  assert.equal(Object.getPrototypeOf(C), realm.function);
}

async function rejected(changed, original, verify, expected) {
  assert.notEqual(changed, original); new vm.SourceTextModule(changed);
  await assert.rejects(verify(changed).then(actual => assert.deepEqual(actual, expected)), {name: 'AssertionError'});
}

export async function verifyRoutesRecord(source, reference) {
  assert.match(source, /let ROUTES = \[\n  \{\n    title: \{/);
  assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED|\$value|\$acc|\(\{|Object\.defineProperty|\.call\(/);
  let cases = 0, mutations = 0, literalNodes;
  for (const hostile of [false, true]) {
    const expected = await routes(reference, hostile);
    assert.deepEqual(await routes(source, hostile), expected);
    literalNodes = expected.nodes; ++cases;
  }
  const expected = await routes(reference, true);
  for (const [changed, hostile] of [
    [source.replace(/id: (\d+)/, (_, id) => `id: ${Number(id) + 1}`), true],
    [source.replace('to: "Shopping"', 'to: "wrong"'), true],
    [`const shared = [];\n${source.replaceAll('params: []', 'params: shared')}`, true],
    [source.replace('params: []', 'params: [,]'), true],
    [source.replace('type: 10003, params:', 'params:'), true],
    [source.replace('title: {id:', 'title: {__proto__: null, id:'), true],
    [source.replace('params: []', 'params: Object.freeze([])'), false],
    [source.replace('export { ROUTES as "ROUTES" };', 'export { ROUTES as "ROUTES", ROUTES as "EXTRA" };'), true],
  ]) {
    await rejected(changed, source, changed => routes(changed, hostile, expected.before), expected); ++mutations;
  }
  return {wholeRecordBehavior: true, wholeRecordStyle: 'accepted', cases, literalNodes, mutations};
}

export async function verifyStaticConstantsRecord(source, reference) {
  assert.match(source, /class CommonConstants \{/);
  assert.match(source, /static TEXT_OPACITY = \[1, 0\.4, 0\.6, 0\.05\]/);
  assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED|\$value|\$acc|Object\.defineProperty|\.call\(/);
  let cases = 0, constructors = 0, faults = 0, mutations = 0;
  for (const hostile of [false, true]) {
    const expected = await staticClass(reference, hostile);
    assert.deepEqual(await staticClass(source, hostile), expected);
    constructors += expected.constructors; ++cases;
  }
  for (const kind of ['class', 'foreign', 'fallback', 'foreign-fallback']) {
    await construction(source, kind, false); await construction(reference, kind, false); ++constructors;
    for (const failure of [undefined, null, 0, Symbol('failure'), {}]) {
      await construction(source, kind, true, failure); await construction(reference, kind, true, failure); ++faults;
    }
  }
  const expected = await staticClass(reference, true);
  for (const changed of [
    source.replace('static FULL_PERCENT = "100%"', 'static FULL_PERCENT = "wrong"'),
    source.replace('static FONT_WEIGHT_500 = 500', 'static FONT_WEIGHT_500 = 501'),
    source.replace('[1, 0.4, 0.6, 0.05]', '[1, , 0.6, 0.05]'),
    source.replace('[1, 0.4, 0.6, 0.05]', '[1, 0.4, 0.6, 0.05, undefined]'),
    source.replace('constructor() {', 'constructor() { this.extra = true;'),
    source.replace('constructor() {', 'constructor(arg) {'),
    source.replace('static TEXT_OPACITY', 'TEXT_OPACITY'),
    source.replace('static FULL_PERCENT = "100%";', 'static { this.FULL_PERCENT = "100%"; }'),
  ]) {
    await rejected(changed, source, changed => staticClass(changed, true), expected); ++mutations;
  }
  return {wholeRecordBehavior: true, wholeRecordStyle: 'accepted', cases, constructors, faults, mutations};
}
