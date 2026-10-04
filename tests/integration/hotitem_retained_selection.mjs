import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Paired source is read only by this independent test, after ABC-only recovery.
// Exercise the Text.width and Row.margin callbacks, not an ArkUI renderer.
const [tool, root] = process.argv.slice(2);
const recovered = JSON.parse(execFileSync(tool, [join(root,'modules.abc'), '47847',
  '--record-source','--value-source'], {encoding:'utf8'}));
assert.equal(recovered.source_complete,true);
assert.equal(recovered.legacy_writer_used,false);
assert.match(recovered.source, /showDetail \?\? false/);
const paired = stripTypeScriptTypes(readFileSync(join(root,
  'src/rank/src/main/ets/view/HotListItemView.ts'),'utf8'));
const inputObject = {[Symbol.toPrimitive]() { assert.fail('coerced retained object'); }};
const inputSymbol = Symbol('width');
async function observe(source, callback, probe, override, replace, failAt) {
  const events = [], registrations = [], failure = {}, resources = new Set();
  let active = false, steps = 0;
  const encode = value => {
    if (value === inputObject) return '<input-object>';
    if (value === inputSymbol) return '<input-symbol>';
    if (value === undefined) return '<undefined>';
    if (typeof value === 'number' && Number.isNaN(value)) return '<NaN>';
    if (Object.is(value,-0)) return '<negative-zero>';
    if (typeof value === 'bigint') return `${value}n`;
    if (value === null || typeof value !== 'object') return value;
    if (Array.isArray(value)) return Array.from(value,encode);
    if (Object.hasOwn(value,'params')) {
      assert.ok(!resources.has(value) && !resources.has(value.params));
      resources.add(value); resources.add(value.params);
      assert.ok(Array.isArray(value.params) && value.params.length === 0);
    }
    const output = {};
    for (const key of Object.keys(value)) {
      const descriptor = Object.getOwnPropertyDescriptor(value,key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
      output[key] = encode(descriptor.value);
    }
    return output;
  };
  const event = (name,args=[],inject=true) => {
    if (!active) return;
    events.push([name,args.map(encode)]);
    if (inject && ++steps === failAt) throw failure;
  };
  const labels = new Map(), components = new Map(), globals = {};
  const component = (name,version=0) => {
    const target = new Proxy({}, {get(_object,key) {
      event(`get:${name}.${String(key)}`,[version]);
      const fn = function(...args) {
        assert.equal(this,target); event(`call:${name}.${String(key)}`,[version,...args]);
      };
      Object.defineProperty(fn,'call',{get() { assert.fail('mutable call helper'); }});
      return fn;
    }});
    labels.set(target,name); return target;
  };
  for (const name of ['Row','Text','Column','Image','If']) {
    components.set(name,component(name));
    Object.defineProperty(globals,name,{get() { event(`global:${name}`,[],false); return components.get(name); }});
  }
  globals.TextAlign = {get End() { event('TextAlign.End'); return 'end'; }};
  globals.FlexAlign = {get Start() { event('FlexAlign.Start'); return 'start'; }};
  const common = new Proxy({FULL_PERCENT:'100%',FONT_WEIGHT_500:500,TOP_COUNT:3}, {
    get(object,key) { event(`common:${String(key)}`); return object[key]; }
  });
  const context = vm.createContext(globals);
  const module = new vm.SourceTextModule(source,{context});
  const dependencies = new Map();
  await module.link(specifier => {
    if (!dependencies.has(specifier)) dependencies.set(specifier,
      new vm.SyntheticModule(['CommonConstants'],function() { this.setExport('CommonConstants',common); },{context}));
    return dependencies.get(specifier);
  });
  await module.evaluate();
  const owner = {observeComponentCreation2(fn,type) {
    assert.equal(this,owner); registrations.push({fn,type:labels.get(type)});
  }};
  const params = (value,label) => ({
    get item() { event(`${label}.item`); return {get index() { event(`${label}.index`); return 1; }}; },
    get indexWidth() {
      event(`${label}.width`); if (replace) components.set('Text',component('Text',1)); return value;
    },
    get indexIconGap() {
      event(`${label}.gap`); if (replace) components.set('Row',component('Row',1)); return value;
    }
  });
  Reflect.apply(module.namespace.HotListItemView,owner,[params(override ? -999 : probe,'captured'),owner]);
  assert.equal(registrations[2].type,'Text'); assert.equal(registrations[3].type,'Row');
  assert.equal(registrations[callback].fn.length,2);
  active = true;
  try {
    Reflect.apply(registrations[callback].fn,{alien:true},[17,true,override ? params(probe,'override') : undefined]);
    return {events};
  } catch (error) { assert.equal(error,failure); return {events,threw:true}; }
}
let scenarios = 0;
for (const callback of [2,3]) for (const probe of [undefined,null,false,0,-0,NaN,3,'width',1n,inputSymbol,inputObject])
  for (const override of [false,true]) for (const replace of [false,true]) for (const failAt of [0,1,5,10,15,20,25]) {
    assert.deepEqual(await observe(recovered.source,callback,probe,override,replace,failAt),
      await observe(paired,callback,probe,override,replace,failAt));
    ++scenarios;
  }
async function detail(source, showDetail, inputIndex, failAt) {
  const events = [], failure = {};
  const event = label => { events.push(label); if (events.length === failAt) throw failure; };
  const common = {get TOP_COUNT() { event('TOP_COUNT'); return 3; }};
  const context = vm.createContext({});
  // Export the existing local only for this test; do not rewrite its body.
  const module = new vm.SourceTextModule(`${source}\nexport {isShowDetail};`, {context});
  await module.link(() => new vm.SyntheticModule(['CommonConstants'], function() {
    this.setExport('CommonConstants', common);
  }, {context}));
  await module.evaluate();
  const index = inputIndex === 'coercion' ? {[Symbol.toPrimitive](hint) {
    assert.equal(hint, 'number'); event('index-conversion'); return 2;
  }} : inputIndex;
  let result;
  try { result = module.namespace.isShowDetail(showDetail, index); }
  catch (error) {
    if (error === failure) result = 'sentinel';
    else if (Object.getPrototypeOf(error) === vm.runInContext('TypeError.prototype', context)) result = 'TypeError';
    else throw error;
  }
  return {events, result};
}
let detailScenarios = 0;
for (const showDetail of [undefined, null, false, 0, -0, NaN, '', true, 1, 'detail', 1n, inputSymbol, inputObject])
  for (const index of [undefined, null, -0, NaN, 2, 3, 4, '2', 2n, inputSymbol, 'coercion'])
    for (const failAt of [0, 1, 2, 3]) {
      assert.deepEqual(await detail(recovered.source, showDetail, index, failAt), await detail(paired, showDetail, index, failAt));
      ++detailScenarios;
    }
assert.equal(detailScenarios, 572);
console.log(`Paired HotListItemView: ${scenarios} callback/default/reference and ${detailScenarios} nullish detail scenarios passed`);
