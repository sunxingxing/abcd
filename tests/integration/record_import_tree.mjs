import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const recovered = execFileSync(process.argv[2], ['source'], {encoding: 'utf8'});
// Independent source model of the fixture's module graph; the production
// process receives only the synthetic ABC, never this reference source.
const reference = `
import './side.mjs';
import {value as input, mutate} from './dep.mjs';
import unused from './dep.mjs';
import * as ns from './dep.mjs';
import './star.mjs';
export {value as live} from './dep.mjs';
export * as depNamespace from './dep.mjs';
export * from './star.mjs';
let answer = input;
let snapshot = ns;
answer = answer;
mutate();
answer = input;
export {answer, answer as answerAlias, snapshot};
`;
async function setup(source, {cycle = false, defaultExport = true, initial = 42, external, namedGlobal = false} = {}) {
  const trace = [];
  const context = vm.createContext({trace, externalCase: external});
  // Define the accessor inside the realm: contextified host accessors can
  // convert a thrown getter into a missing-binding ReferenceError in Node.
  if (external) vm.runInContext(`Object.defineProperty(globalThis, 'external', {get() {
    trace.push('external');
    if (externalCase.failure) throw externalCase.failure;
    return externalCase.value;
  }})`, context);
  if (namedGlobal) vm.runInContext(`Object.defineProperty(globalThis,'capturedValue',{get() {
    trace.push('captured-global'); return 17;
  }})`,context);
  const create = (body, identifier) => new vm.SourceTextModule(body, {context, identifier});
  const root = create(source, './root.mjs');
  const dependency = create(`
    ${cycle ? "import {answer} from './root.mjs'; trace.push(answer);" : ''}
    trace.push('dep'); export let value = ${initial};
    export function mutate() {
      if (this !== undefined) throw new Error('receiver changed');
      trace.push('mutate'); ++value;
    }
    ${defaultExport ? 'export default 7;' : ''}
  `, './dep.mjs');
  const modules = new Map([
    ['./root.mjs', root], ['./dep.mjs', dependency],
    ['./side.mjs', create("trace.push('side');", './side.mjs')],
    ['./star.mjs', create("trace.push('star'); export const starValue = 9; export const live = -1; export default 0;", './star.mjs')],
  ]);
  await root.link(specifier => {
    assert.ok(modules.has(specifier), specifier);
    return modules.get(specifier);
  });
  return {root, dependency, trace};
}
async function observe(source, conditional = false, initial = 42) {
  const {root, dependency, trace} = await setup(source, {initial});
  const mutations = 1 + Number(conditional && initial !== 0);
  const final = initial + mutations;
  assert.throws(() => root.namespace.answer, {name: 'ReferenceError'});
  await root.evaluate();
  const ns = root.namespace;
  assert.deepEqual(trace, ['side', 'dep', 'star', ...Array(mutations).fill('mutate')]);
  assert.deepEqual(Object.keys(ns), ['answer', 'answerAlias', 'depNamespace', 'live', 'snapshot', 'starValue']);
  assert.equal(ns.answer, final); assert.equal(ns.answerAlias, final); assert.equal(ns.live, final);
  assert.equal(ns.starValue, 9);
  assert.equal(ns.snapshot, dependency.namespace);
  assert.equal(ns.depNamespace, dependency.namespace);
  assert.equal(Reflect.set(ns, 'answer', 0), false);
  Reflect.apply(dependency.namespace.mutate, undefined, []);
  assert.equal(ns.live, final + 1); assert.equal(ns.snapshot.value, final + 1);
  assert.equal(ns.answer, final); assert.equal(ns.answerAlias, final);
  return {trace, answer: ns.answer, live: ns.live, keys: Object.keys(ns)};
}
assert.deepEqual(await observe(recovered), await observe(reference));
for (const source of [recovered, reference]) {
  const {root} = await setup(source, {cycle: true});
  await assert.rejects(root.evaluate(), {name: 'ReferenceError'});
  // Even unused imports retain their link-time export obligation.
  await assert.rejects(setup(source, {defaultExport: false}), {name: 'SyntaxError'});
}

const conditional = execFileSync(process.argv[2], ['source', 'branch'], {encoding: 'utf8'});
const conditionalReference = reference.replace('let answer = input;', 'if (input !== 0) mutate();\nlet answer = input;');
for (const initial of [0, 42])
  assert.deepEqual(await observe(conditional, true, initial), await observe(conditionalReference, true, initial));
const cyclic = await setup(conditional, {cycle: true});
await assert.rejects(cyclic.root.evaluate(), {name: 'ReferenceError'});

for (const mode of ['--function', '--capture', '--local-function', '--class-function', '--class-capture']) {
  const generated = execFileSync(process.argv[2], [mode], {encoding: 'utf8'});
  const mixed = mode === '--class-function' || mode === '--class-capture';
  const tail = mode === '--class-capture' ? 'let captured = snapshot; answer = function reader() { return captured; };' : mode === '--capture' ? `
    let captured = 1;
    answer = function reader() { return captured; };
    captured = 42;
  ` : mode === '--local-function' ? 'answer = function reader() { return answer; };' :
      'answer = function reader() { return input; };';
  const expected = reference + (mixed ? 'snapshot = class Box {};' : '') + tail;
  for (const source of [generated, expected]) {
    const {root, dependency, trace} = await setup(source);
    assert.throws(() => root.namespace.answer, {name: 'ReferenceError'});
    await root.evaluate();
    const fn = root.namespace.answer;
    assert.equal(fn, root.namespace.answerAlias);
    assert.equal(fn.name, 'reader'); assert.equal(fn.length, 0);
    assert.equal(fn(), mode === '--class-capture' ? root.namespace.snapshot : mode === '--local-function' ? fn : mode === '--capture' ? 42 : 43);
    Reflect.apply(dependency.namespace.mutate, undefined, []);
    assert.equal(fn(), mode === '--class-capture' ? root.namespace.snapshot : mode === '--local-function' ? fn : mode === '--capture' ? 42 : 44);
    assert.equal(root.namespace.answer, fn);
    if (mixed) {
      const Box = root.namespace.snapshot;
      assert.equal(Box.name, 'Box'); assert.equal(Box.length, 0);
      const first = new Box(), second = new Box();
      assert.notEqual(first, second); assert.equal(Object.getPrototypeOf(first), Box.prototype);
      assert.equal(Box.prototype.constructor, Box);
      assert.equal(Object.getOwnPropertyDescriptor(Box, 'prototype').writable, false);
      assert.throws(() => Box(), {name: 'TypeError'});
      assert.equal(root.namespace.snapshot, Box);
    } else assert.equal(root.namespace.snapshot, dependency.namespace);
    assert.deepEqual(trace, ['side', 'dep', 'star', 'mutate', 'mutate']);
    const cyclic = await setup(source, {cycle: true});
    await assert.rejects(cyclic.root.evaluate(), {name: 'ReferenceError'});
  }
}

const globalSource = execFileSync(process.argv[2], ['--global'], {encoding: 'utf8'});
const globalReference = reference.replace('let answer = input;', 'external;\nlet answer = input;');
const throwingGlobalTraces = [];
for (const source of [globalSource, globalReference]) {
  for (const value of [undefined, null, 42, {}]) {
    const {root, trace} = await setup(source, {external: {value}});
    await root.evaluate();
    assert.deepEqual(trace, ['side', 'dep', 'star', 'external', 'mutate']);
    assert.equal(root.namespace.answer, 43);
  }
  const failure = {};
  const throwing = await setup(source, {external: {failure}});
  await assert.rejects(throwing.root.evaluate(), error => error === failure);
  throwingGlobalTraces.push(throwing.trace);
  assert.equal(throwing.trace.includes('mutate'), false);
  const missing = await setup(source);
  await assert.rejects(missing.root.evaluate(), {name: 'ReferenceError'});
  assert.deepEqual(missing.trace, ['side', 'dep', 'star']);
}
// Node's contextified global lookup can retry an accessor that throws;
// compare the two independent programs in the same realm implementation.
assert.deepEqual(throwingGlobalTraces[0], throwingGlobalTraces[1]);

// A record with only a class still owns a lexical cell. Its constructor must
// capture that cell, not substitute a later live read of the imported binding.
const classCellSource = execFileSync(process.argv[2], ['--class-cell'], {encoding:'utf8'});
const classCellReference = reference.slice(0, reference.indexOf('let answer')) + `
export default input;
let snapshot = ns;
let captured = input;
snapshot = class Box { constructor() { return captured; } };
export {snapshot};
`;
async function observeClassCell(source, initial, namedGlobal = false) {
  const {root, dependency, trace} = await setup(source, {initial, namedGlobal});
  await root.evaluate();
  const Box = root.namespace.snapshot, captured = root.namespace.default;
  const object = captured !== null && ['object','function'].includes(typeof captured);
  assert.equal(Box.name,'Box'); assert.equal(Box.length,0);
  class Child extends Box {}
  const check = () => {
    const first = new Box(), second = new Box(), inherited = new Child();
    if (object) { assert.equal(first,captured); assert.equal(second,captured); assert.equal(inherited,captured); }
    else {
      assert.notEqual(first,second);
      assert.equal(Object.getPrototypeOf(first),Box.prototype);
      assert.equal(Object.getPrototypeOf(inherited),Child.prototype);
    }
    assert.throws(() => Box(), {name:'TypeError'});
  };
  check();
  try { Reflect.apply(dependency.namespace.mutate,undefined,[]); }
  catch (error) { assert.equal(error.name,'TypeError'); trace.push('mutation-type-error'); }
  check();
  return {trace, keys:Object.keys(root.namespace), object};
}
for (const initial of ['42','undefined','null','({marker:1})','(()=>7)','1n','Symbol("x")',
    '({valueOf(){trace.push("valueOf");return 7;}})'])
  assert.deepEqual(await observeClassCell(classCellSource,initial), await observeClassCell(classCellReference,initial));

const namedClassCellSource = execFileSync(process.argv[2], ['--class-named-cell'], {encoding:'utf8'});
const shadowClassCellSource = execFileSync(process.argv[2], ['--class-shadow-cell'], {encoding:'utf8'});
const shadowClassReference = classCellReference.replace('constructor() { return captured; }',
    'constructor() { capturedValue; return captured; }');
for (const initial of ['42','({marker:1})','(()=>7)']) {
  assert.deepEqual(await observeClassCell(namedClassCellSource,initial), await observeClassCell(classCellReference,initial));
  assert.deepEqual(await observeClassCell(shadowClassCellSource,initial,true), await observeClassCell(shadowClassReference,initial,true));
}

const publishedCapture = execFileSync(process.argv[2], ['--published-capture'], {encoding:'utf8'});
const publishedReference = reference.replace('export {answer, answer as answerAlias, snapshot};', `
let captured;
answer = function reader() { return captured; };
captured = 42;
export {answer, answer as answerAlias, snapshot};
`);
for (const source of [publishedCapture,publishedReference]) {
  const {root,dependency,trace} = await setup(source);
  await root.evaluate();
  const reader=root.namespace.answer;
  assert.equal(reader(),42); assert.equal(root.namespace.answerAlias,reader);
  Reflect.apply(dependency.namespace.mutate,undefined,[]);
  assert.equal(reader(),42); assert.equal(root.namespace.answer,reader);
  assert.deepEqual(trace,['side','dep','star','mutate','mutate']);
}

const capturedDefault = execFileSync(process.argv[2], ['--captured-default'], {encoding:'utf8'});
const defaultReference = reference.replace('export {answer, answer as answerAlias, snapshot};', `
let captured = 1;
answer = function reader(value = captured) { return value; };
captured = 42;
export {answer, answer as answerAlias, snapshot};
`);
for (const source of [capturedDefault,defaultReference]) {
  const {root,trace} = await setup(source);
  await root.evaluate();
  const reader = root.namespace.answer;
  assert.equal(reader.length,0);
  assert.equal(reader(),42); assert.equal(reader(undefined),42);
  const hostile = new Proxy({}, {get() { assert.fail('default coerced the argument'); }});
  for (const value of [null,false,true,0,-0,NaN,Infinity,3,'text',7n,Symbol('identity'),{},()=>{},hostile])
    assert.ok(Object.is(reader(value),value));
  assert.equal(reader(undefined,hostile),42);
  assert.equal(root.namespace.answerAlias,reader);
  assert.deepEqual(trace,['side','dep','star','mutate']);
}
console.log('Captured defaults: current cell, identity, length and no argument coercion passed');
