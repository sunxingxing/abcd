import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const tool = process.argv[2];
const imports = `import './side.mjs'; import {value as input,mutate} from './dep.mjs';
  import unused from './dep.mjs'; import * as ns from './dep.mjs'; import './star.mjs';
  export {value as live} from './dep.mjs'; export * as depNamespace from './dep.mjs';
  export * from './star.mjs';`;
const hooks = `export function read() {return snapshot;}
  export function write(value) {snapshot = value;}`;
const entry = `events.push('entry'); try {snapshot; events.push('ready');}
  catch (error) {events.push(error.name);}`;
const reference = outerRead => `${imports}
  mutate();
  let snapshot = class {constructor() {${outerRead ? 'return snapshot;' : ''}}};
  const answer = snapshot; snapshot = input;
  export {answer,answer as answerAlias,snapshot};`;

async function observe(source, outerRead, value) {
  const replacement = Object.freeze({replacement:true}), after = Object.freeze({after:true});
  const events = [], context = vm.createContext({events,initial:value,replacement});
  const dep = new vm.SourceTextModule(`export let value = initial; export default 7;
    export function mutate() {events.push('mutate'); value = replacement;}`,{context});
  const side = new vm.SourceTextModule(`events.push('side');`,{context});
  const star = new vm.SourceTextModule(`events.push('star'); export const starValue = 9;`,{context});
  const module = new vm.SourceTextModule(`${entry}\n${source}\n${hooks}`,{context});
  await module.link(name => name === './dep.mjs' ? dep : name === './side.mjs' ? side : star);
  for (const name of ['answer','answerAlias','snapshot'])
    assert.throws(() => module.namespace[name],{name:'ReferenceError'});
  await module.evaluate();
  const C = module.namespace.answer;
  assert.equal(C,module.namespace.answerAlias);
  assert.equal(module.namespace.snapshot,replacement);
  assert.equal(C.name,'snapshot'); assert.equal(C.length,0);
  assert.equal(module.namespace.depNamespace,dep.namespace);
  assert.equal(module.namespace.live,replacement); assert.equal(module.namespace.starValue,9);
  assert.throws(() => Reflect.apply(C,{},[]),{name:'TypeError'});
  assert.deepEqual(Reflect.ownKeys(C.prototype),['constructor']);
  const first = new C();
  if (outerRead) assert.equal(first,replacement);
  else {
    assert.equal(Object.getPrototypeOf(first),C.prototype);
    assert.deepEqual(Reflect.ownKeys(first),[]);
    assert.notEqual(new C(),first);
  }
  function Target() {} Target.prototype = Object.freeze({alternate:true});
  const alternate = Reflect.construct(C,[value],Target);
  assert.equal(Object.getPrototypeOf(alternate),outerRead ? Object.getPrototypeOf(replacement) : Target.prototype);
  module.namespace.write(after);
  assert.equal(module.namespace.read(),after); assert.equal(module.namespace.snapshot,after);
  assert.equal(module.namespace.answer,C);
  const next = new C();
  if (outerRead) assert.equal(next,after);
  else assert.equal(Object.getPrototypeOf(next),C.prototype);
  await module.evaluate(); assert.equal(module.namespace.answer,C);
  return {events,name:C.name,length:C.length};
}

// Fixture bodies and profiles come through actual ABC decoding and record
// composition. Copies/splits may retain required creation-time temporaries.
const fixtures = [];
for (const profile of ['1202','1206','1301']) for (const copies of [0,1,3])
  for (const split of [false,true]) for (const outerRead of [false,true]) {
    const source = execFileSync(tool,[outerRead ? '--class-declaration-outer-read' : '--class-declaration',
      profile,String(copies),...(split ? ['split'] : [])],{encoding:'utf8'});
    assert.doesNotMatch(source,/INCOMPLETE|UNRECOVERED/);
    if (!outerRead && !split && copies <= 1) assert.match(source,/(^|\n)class snapshot \{/);
    if (outerRead && !split && copies <= 1) assert.match(source,/let snapshot = class \{/);
    fixtures.push({source,outerRead,profile,copies,split});
  }
let cases = 0;
for (const {source,outerRead} of fixtures) for (const value of [undefined,null,false,-0,NaN,3n,Symbol('value'),{},[]]) {
  // Both independent observations receive the same host identities. Reference
  // imports/body are authored here, never obtained from the generated source.
  const expected = reference(outerRead);
  assert.deepEqual(await observe(source,outerRead,value),await observe(expected,outerRead,value)); ++cases;
}
const safe = fixtures.find(item => !item.outerRead && !item.split && item.copies === 0).source;
assert.match(safe,/\bclass snapshot \{/); assert.doesNotMatch(safe,/let snapshot = class/);
const unsafe = fixtures.find(item => item.outerRead && !item.split && item.copies === 0).source;
const mutations = [
  {source:unsafe,change:text => text.replace('let snapshot = class {','class snapshot {'),outerRead:true},
  {source:safe,change:text => text.replace('class snapshot {','function snapshot() {').replace(
    '  constructor() {\n  }\n',''),outerRead:false},
  {source:safe,change:text => text.replace('snapshot = input','snapshot = 7'),outerRead:false},
  {source:safe,change:text => text.replace('(answer = snapshot);',
    'Object.defineProperty(snapshot, "name", {value:"WrongName"});\n(answer = snapshot);'),outerRead:false},
];
for (const {source,change,outerRead} of mutations) {
  const changed = change(source); assert.notEqual(changed,source);
  new vm.SourceTextModule(changed);
  await assert.rejects(observe(changed,outerRead,{}),assert.AssertionError);
}

async function observeDerived(source, mode) {
  const events = [], original = Symbol('parent throw'), returned = Object.freeze({returned:true});
  let active;
  function Base(...args) {
    assert.deepEqual(args,[]); events.push(`construct:${new.target.name}`);
    if (mode === 'throw') throw original;
    if (mode === 'object-return') return returned;
    this.mark = 'base';
  }
  let parent = Base;
  if (mode === 'bound') {
    parent = Base.bind(null); Object.defineProperty(parent,'prototype',{value:Base.prototype});
  } else if (mode === 'proxy' || mode === 'prototype-throw') parent = new Proxy(Base,{
    get(target,key,receiver) {
      if (key === 'prototype') {events.push('parent-prototype'); if (mode === 'prototype-throw') throw original;}
      return Reflect.get(target,key,receiver);
    },
    construct(target,args,newTarget) {
      events.push('parent-construct'); return Reflect.construct(target,args,newTarget);
    },
  });
  else if (mode === 'null-parent') parent = null;
  else if (mode === 'invalid-parent') parent = 7;
  const context = vm.createContext({events,parent});
  const dep = new vm.SourceTextModule(`export let value = parent; export default 7;
    export function mutate() {events.push('mutate');}`,{context});
  const side = new vm.SourceTextModule(`events.push('side');`,{context});
  const star = new vm.SourceTextModule(`events.push('star'); export const starValue = 9;`,{context});
  const module = new vm.SourceTextModule(`${entry}\n${source}\n${hooks}`,{context});
  await module.link(name => name === './dep.mjs' ? dep : name === './side.mjs' ? side : star);
  try {await module.evaluate();}
  catch (error) {
    if (mode === 'prototype-throw') assert.equal(error,original);
    else {assert.equal(mode,'invalid-parent'); assert.equal(error.name,'TypeError');}
    assert.throws(() => module.namespace.snapshot,{name:'ReferenceError'});
    return {events,evaluationFailure:true};
  }
  assert.notEqual(mode,'prototype-throw'); assert.notEqual(mode,'invalid-parent');
  const C = module.namespace.answer; active = C;
  assert.equal(C.name,'snapshot'); assert.equal(C.length,0);
  assert.equal(Object.getPrototypeOf(C),parent === null ? vm.runInContext('Function.prototype',context) : parent);
  assert.equal(Object.getPrototypeOf(C.prototype),parent === null ? null : parent.prototype);
  assert.equal(module.namespace.read(),parent);
  assert.throws(() => Reflect.apply(C,null,[]),{name:'TypeError'});
  const value = {};
  function Target() {} Target.prototype = {alternate:true};
  for (const target of [C,Target]) {
    try {
      const result = Reflect.construct(C,[value],target);
      assert.notEqual(mode,'throw'); assert.notEqual(mode,'null-parent');
      if (mode === 'object-return') assert.equal(result,returned);
      else {assert.equal(Object.getPrototypeOf(result),target.prototype); assert.equal(result.mark,'base');}
    } catch (error) {
      if (mode === 'throw') assert.equal(error,original);
      else {assert.equal(mode,'null-parent'); assert.equal(error.name,'TypeError');}
    }
  }
  function Current(...args) {
    assert.deepEqual(args,[]); assert.equal(new.target,active);
    events.push('current-parent'); this.mark = 'current';
  }
  const current = new Proxy(Current,{construct(target,args,newTarget) {
    events.push('current-proxy'); return Reflect.construct(target,args,newTarget);
  }});
  Object.setPrototypeOf(C,current);
  const instance = new C(value); assert.equal(instance.mark,'current');
  assert.equal(Object.getPrototypeOf(instance),C.prototype);
  module.namespace.write(value); assert.equal(module.namespace.read(),value);
  assert.equal(module.namespace.answer,C); assert.equal(module.namespace.answerAlias,C);
  return {events,evaluationFailure:false};
}
let derivedRecords = 0;
let derivedSample;
const derivedReference = `${imports} mutate();
  let snapshot = class extends (input) {constructor() {return super(...[]);}};
  const answer = snapshot; snapshot = input; export {answer,answer as answerAlias,snapshot};`;
for (const profile of ['1202','1206','1301']) for (const copies of [0,1,3]) for (const split of [false,true]) {
  const source = execFileSync(tool,['--class-declaration-derived',profile,String(copies),...(split ? ['split'] : [])],
    {encoding:'utf8'});
  assert.doesNotMatch(source,/INCOMPLETE|UNRECOVERED/);
  if (!split && copies <= 1) assert.match(source,/(^|\n)class snapshot extends/);
  if (profile === '1206' && copies === 0 && !split) derivedSample = source;
  ++derivedRecords;
  for (const mode of ['ordinary','bound','proxy','object-return','throw','prototype-throw','null-parent','invalid-parent']) {
    assert.deepEqual(await observeDerived(source,mode),await observeDerived(derivedReference,mode)); ++cases;
  }
}
for (const replacement of ['Reflect.construct(input, [], new.target)','super(...arguments)']) {
  const changed = derivedSample.replace('super(...[])',replacement);
  assert.notEqual(changed,derivedSample); new vm.SourceTextModule(changed);
  await assert.rejects(observeDerived(changed,'ordinary'),assert.AssertionError);
}
console.log(JSON.stringify({decodedRecords:fixtures.length + derivedRecords,cases,mutations:mutations.length + 2}));
