import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// ABC-only recovery precedes the independent paired oracle. This checks header
// registration and its Text callback; ForEach, child views and rendering are excluded.
const [tool,root] = process.argv.slice(2);
const output = JSON.parse(execFileSync(tool,[join(root,'modules.abc'),'48609','--record-source','--value-source'],{encoding:'utf8'}));
assert.equal(output.source_complete,true); assert.equal(output.legacy_writer_used,false);
const paired = stripTypeScriptTypes(readFileSync(join(root,'src/rank/src/main/ets/view/HotListView.ts'),'utf8'));
const token = Symbol('title'), overrideValue = {};
const hostile = {[Symbol.toPrimitive]() { assert.fail('coerced title'); }};
async function observe(source,title,parentMode,override,failAt) {
  const events = [], registrations = [], headers = [], failure = {}, labels = new Map();
  let active = false, steps = 0;
  const encode = value => {
    if (value === hostile) return '<hostile>';
    if (value === token) return '<symbol>';
    if (value === overrideValue) return '<override>';
    if (value === undefined) return '<undefined>';
    if (Object.is(value,-0)) return '<negative-zero>';
    if (typeof value === 'number' && Number.isNaN(value)) return '<NaN>';
    if (typeof value === 'bigint') return `${value}n`;
    if (typeof value === 'function') return '<function>';
    if (value === null || typeof value !== 'object') return value;
    if (Array.isArray(value)) return Array.from(value,encode);
    const result = {};
    for (const key of Object.keys(value)) {
      const descriptor = Object.getOwnPropertyDescriptor(value,key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
      result[key] = encode(descriptor.value);
    }
    return result;
  };
  const event = (name,args=[]) => {
    if (!active) return;
    events.push([name,args.map(encode)]);
    if (++steps === failAt) throw failure;
  };
  const globals = {};
  for (const name of ['Row','List','ListItemGroup','ForEach','Text']) {
    const component = new Proxy({}, {get(_target,key) {
      event(`get:${name}.${String(key)}`);
      return function(...args) {
        assert.equal(this,component); event(`call:${name}.${String(key)}`,args);
        if (name === 'ListItemGroup' && key === 'create') {
          assert.deepEqual(Object.keys(args[0]),['header']); headers.push(args[0].header);
        }
      };
    }});
    globals[name] = component; labels.set(component,name);
  }
  const context = vm.createContext(globals);
  const common = {get FONT_WEIGHT_700() { event('common:FONT_WEIGHT_700'); return 700; }};
  const unavailable = () => assert.fail('outside header callback scope');
  const exports = {CommonConstants:common,HOST_LIST_ARRAY:new Proxy({}, {get:unavailable}),HotListItemView:unavailable};
  const dependencies = new Map();
  const module = new vm.SourceTextModule(source,{context});
  await module.link(specifier => {
    if (!dependencies.has(specifier)) dependencies.set(specifier,new vm.SyntheticModule(Object.keys(exports),function() {
      for (const [key,value] of Object.entries(exports)) this.setExport(key,value);
    },{context}));
    return dependencies.get(specifier);
  });
  await module.evaluate();
  const owner = name => {
    const value = {observeComponentCreation2(fn,type) {
      assert.equal(this,value); event(`${name}:register`,[labels.get(type),fn.length]);
      registrations.push({fn,type:labels.get(type),owner:value});
    }};
    return value;
  };
  const receiver = owner('receiver'), parent = owner('parent');
  assert.equal(module.namespace.HotListView.length,1);
  const args = parentMode === 0 ? [1,title] : [1,title,parentMode === 1 ? null : parent];
  Reflect.apply(module.namespace.HotListView,receiver,args);
  assert.equal(registrations.length,4);
  assert.ok(registrations.every(item => item.owner === (parentMode === 2 ? parent : receiver)));
  assert.equal(registrations[2].type,'ListItemGroup');
  active = true;
  try {
    Reflect.apply(registrations[2].fn,{alien:true},[17,true]);
    assert.equal(headers.length,1); assert.equal(headers[0].length,0);
    Reflect.apply(headers[0],{ignored:true},[]);
    const nested = registrations.slice(4);
    assert.equal(nested.length,2); assert.ok(nested.every(item => item.owner === receiver));
    const text = nested.find(item => item.type === 'Text'); assert.ok(text); assert.equal(text.fn.length,2);
    Reflect.apply(text.fn,{alien:true},[18,true,override ? overrideValue : undefined]);
    return {events};
  } catch (error) { assert.equal(error,failure); return {events,threw:true}; }
}
let scenarios = 0;
for (const title of [undefined,'',null,false,-0,NaN,12,1n,token,hostile,'quote " and newline\n'])
  for (const parent of [0,1,2]) for (const override of [false,true]) for (const failAt of [0,1,3,5,8,12,16]) {
    assert.deepEqual(await observe(output.source,title,parent,override,failAt),await observe(paired,title,parent,override,failAt));
    ++scenarios;
  }
console.log(`Paired HotListView: ${scenarios} header/default/receiver/exception scenarios passed`);
