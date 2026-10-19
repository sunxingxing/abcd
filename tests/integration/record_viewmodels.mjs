import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// ABC-only generation has finished before any independent paired TS is read.
// Boundary probes execute actual ViewModel bodies against instrumented model
// APIs and native slice. Connected tests execute all four actual records. These
// controlled resources are not a HarmonyOS resource/UI implementation.
const [iconPath, sectionPath, iconModelPath, sectionModelPath, pairedRoot] = process.argv.slice(2);
const sources = [iconPath, sectionPath, iconModelPath, sectionModelPath].map(p => readFileSync(p, 'utf8'));
const paired = ['viewmodel/IconInfoViewModel.ts', 'viewmodel/SectionViewModel.ts',
  'model/IconInfoModel.ts', 'model/SectionModel.ts'].map(p =>
  stripTypeScriptTypes(readFileSync(join(pairedRoot, p), 'utf8')));
const specifier = name => `@normalized:N&&&shopping/src/main/ets/${name}&1.0.0`;
const configs = [
  {view: 'IconInfoViewModel', model: 'IconInfoModel', item: 'IconInfo', field: 'iconInfoModel',
   fields: ['info', 'icon'], getters: ['getInfo', 'getIcon'], groups: [
    {field: 'categoryList', method: 'getCategoryList', input: 'CATEGORY_INFO_LIST', args: ['CATEGORY_ICON_LIST'], slice: false},
    {field: 'welfareList', method: 'getWelfareList', input: 'WELFARE_PRICE_LIST', args: ['WELFARE_ICON_LIST'], slice: true}]},
  {view: 'SectionViewModel', model: 'SectionModel', item: 'SectionProduct', field: 'model',
   fields: ['productImg', 'title', 'price', 'feature', 'comments', 'info', 'originalPrice'],
   getters: ['getProductImg', 'getTitle', 'getPrice', 'getFeature', 'getComments', 'getInfo', 'getOriginalPrice'], groups: [
    {field: 'sectionProductList', method: 'getSectionProductList', input: 'SECTION_PRODUCT_IMAGES',
     args: ['SECTION_PRODUCTS_TITLES', 'SECTION_PRODUCTS_PRICES', 'SECTION_PRODUCTS_FEATURES', 'SECTION_PRODUCTS_COMMENTS', 'SECTION_PRODUCTS_INFO'], slice: true},
    {field: 'flashSaleProductList', method: 'getFlashSaleProductList', input: 'FLASH_SALE_PRODUCT_IMAGES',
     args: ['FLASH_SALE_PRODUCT_TITLES', 'FLASH_SALE_PRODUCT_PRICES', 'FLASH_SALE_PRODUCT_FEATURES', 'FLASH_SALE_PRODUCT_INFO', 'FLASH_SALE_PRODUCT_ORIGINAL_PRICES'], slice: true}]},
];
function poison(fn) {
  for (const key of ['call', 'apply', 'bind']) Object.defineProperty(fn, key, {
    get() { throw new Error(`unexpected ${key} lookup`); },
  });
  return fn;
}
async function loadView(text, config, imported, context = vm.createContext({})) {
  const dependency = new vm.SyntheticModule([config.model], function() {
    this.setExport(config.model, imported);
  }, {context});
  const module = new vm.SourceTextModule(text, {context});
  await module.link(name => { assert.equal(name, specifier(`model/${config.model}`)); return dependency; });
  assert.throws(() => module.namespace[config.view], {name: 'ReferenceError'});
  await module.evaluate();
  assert.deepEqual(Object.keys(module.namespace), [config.view]);
  return {C: module.namespace[config.view], dependency};
}
function style(text, config) {
  assert.match(text, /constructor\(\)/);
  assert.match(text, new RegExp(`this\\.${config.field} = new ${config.model}\\(\\)`));
  for (const group of config.groups) {
    assert.match(text, new RegExp(`${group.method}\\(${group.slice ? 'count' : ''}\\)`));
    if (group.slice) assert.match(text, new RegExp(`\\.${group.method}\\(\\)\\.slice\\(0, count\\)`));
  }
  assert.doesNotMatch(text, /INCOMPLETE|UNRECOVERED|Reflect\.construct|\.call\(|\bswitch\s*\(/);
}

async function boundary(text, config, group, size, count, mode, throwAt = null) {
  const trace = [], points = new Set(), failure = {};
  let instance, dependency, lastSpecies, model, constructed = 0;
  const event = name => {
    trace.push(name); points.add(name); if (name === throwAt) throw failure;
  };
  const raw = Array.from({length: size}, (_, index) => Object.freeze({index}));
  if (size > 1) delete raw[1]; // Native slice must preserve holes, not fill them.
  let list;
  list = new Proxy(raw, {get(target, key, receiver) {
    if (key === 'length' || /^(0|[1-9][0-9]*)$/.test(String(key))) event(`list:${String(key)}`);
    return Reflect.get(target, key, receiver);
  }, has(target, key) {
    if (/^(0|[1-9][0-9]*)$/.test(String(key))) event(`has:${String(key)}`);
    return Reflect.has(target, key);
  }});
  const species = {get [Symbol.species]() {
    event('species:get');
    return function(length) { event(`species:construct:${length}`); assert.ok(Number.isInteger(length) && length >= 0); return lastSpecies = []; };
  }};
  Object.defineProperty(raw, 'constructor', {get() { event('constructor:get'); return species; }});
  Object.defineProperty(raw, 'slice', {get() {
    event('slice:get');
    if (mode === 'slice-switch') Object.defineProperty(instance, config.field, {value: {}});
    return poison(function(start, end) {
      assert.equal(this, list); assert.equal(arguments.length, 2); assert.equal(start, 0);
      event('slice:call'); return Reflect.apply(Array.prototype.slice, this, [start, end]);
    });
  }});
  function Model() {
    assert.equal(arguments.length, 0); event('model:construct'); ++constructed; model = this;
    if (mode === 'constructor-live') dependency.setExport(config.model, function LaterModel() { event('model:later'); });
  }
  let thrown, result;
  const {C, dependency: dep} = await loadView(text, config,
    mode === 'before-construct' ? function StaleModel() { throw new Error('stale imported constructor'); } : Model);
  dependency = dep;
  if (mode === 'before-construct') dependency.setExport(config.model, Model);
  assert.equal(C.name, config.view); assert.equal(C.length, 0);
  const method = Object.getOwnPropertyDescriptor(C.prototype, group.method);
  assert.equal(method.value.name, group.method); assert.equal(method.value.length, group.slice ? 1 : 0);
  assert.deepEqual([method.enumerable, method.writable, method.configurable], [false, true, true]);
  assert.throws(() => C(), {name: 'TypeError'});
  try {
    instance = new C(); assert.equal(constructed, 1); assert.equal(instance[config.field], model);
    const field = Object.getOwnPropertyDescriptor(instance, config.field);
    assert.deepEqual([field.writable, field.enumerable, field.configurable], [true, true, true]);
    Object.defineProperty(model, group.method, {configurable: true, get() {
      assert.equal(this, model); event('method:get');
      if (mode === 'method-switch') Object.defineProperty(model, group.method, {value: () => { throw new Error('wrong callee'); }});
      return poison(function() { assert.equal(this, model); assert.equal(arguments.length, 0); event('method:call'); return list; });
    }});
    Object.defineProperty(instance, config.field, {configurable: true, get() {
      assert.equal(this, instance); event('field:get');
      if (mode === 'field-switch') Object.defineProperty(instance, config.field, {value: {}});
      return model;
    }});
    let arg = count;
    if (count === 'coercible' || count === 'coercion-throw') arg = {
      valueOf() {
        event('count:valueOf');
        Object.defineProperty(instance, config.field, {value: {}});
        dependency.setExport(config.model, function Later() {});
        if (count === 'coercion-throw') throw failure;
        if (raw.length) raw[0] = 99;
        return 2;
      },
    };
    result = Reflect.apply(method.value, instance, group.slice ? [arg] : []);
    if (group.slice) {
      assert.equal(result, lastSpecies); assert.notEqual(result, list);
      for (const key of Object.keys(result)) assert.equal(result[key], raw[key]);
    } else assert.equal(result, list);
    if (mode === 'constructor-live') {
      const next = new C(); assert.notEqual(next[config.field], model); assert.equal(constructed, 1);
    }
  } catch (error) {
    if (error === failure) thrown = 'original';
    else if (error?.name === 'TypeError') thrown = 'TypeError';
    else throw error;
  }
  assert.equal(thrown === 'original', throwAt !== null || (group.slice && count === 'coercion-throw'));
  assert.equal(thrown === 'TypeError', throwAt === null && group.slice &&
    (typeof count === 'bigint' || typeof count === 'symbol'));
  // Observation must not add proxy events or throw outside the method probe.
  const observed = result === list ? raw : result;
  const output = observed ? {length: observed.length, keys: Object.keys(observed), values: Array.from(observed, value => typeof value === 'object' ? value.index : value)} : null;
  return {trace, points: [...points], output, thrown, constructed};
}

// Keep pinned assignment effects separate from Node's initialized TS fields,
// which use DefineField and bypass an inherited setter.
async function assignment(text, config, abrupt, alternate) {
  const trace = [], failure = {}, models = [];
  function Model() { assert.equal(arguments.length, 0); trace.push('construct'); models.push(this); }
  const {C} = await loadView(text, config, Model);
  const prototype = alternate ? {} : C.prototype;
  let receiver;
  Object.defineProperty(prototype, config.field, {set(value) {
    trace.push('set'); receiver = this; assert.equal(value, models.at(-1)); if (abrupt) throw failure;
  }});
  function Target() {}
  const target = new Proxy(Target, {get(object, key, objectReceiver) {
    if (key === 'prototype') { trace.push('prototype'); return prototype; }
    return Reflect.get(object, key, objectReceiver);
  }});
  const construct = () => Reflect.construct(C, [], alternate ? target : C);
  if (abrupt) assert.throws(construct, error => error === failure);
  else {
    const first = construct(), second = construct();
    assert.equal(receiver, second); assert.notEqual(first, second); assert.notEqual(models[0], models[1]);
    assert.equal(Object.getPrototypeOf(first), prototype); assert.equal(Object.hasOwn(first, config.field), false);
  }
  const sequence = [...(alternate ? ['prototype'] : []), 'construct', 'set'];
  assert.deepEqual(trace, abrupt ? sequence : [...sequence, ...sequence]);
}

async function connected(texts, size, mode, throwAt = null, occurrence = 1) {
  const context = vm.createContext({}), trace = [], points = new Set(), failure = {}, models = [], views = [], pending = [], identities = new WeakMap();
  let dependency, matching = 0;
  const event = name => {
    trace.push(name); points.add(name);
    if (name === throwAt && ++matching === occurrence) throw failure;
  };
  function constants(tag) {
    const result = {};
    for (const config of configs) for (const group of config.groups) for (const key of [group.input, ...group.args]) {
      const values = Array.from({length: size}, (_, index) => {
        const value = Object.freeze({tag, key, index}); identities.set(value, [tag, key, index]); return value;
      });
      if (mode === 'sparse' && size > 1) delete values[1];
      let array;
      array = new Proxy(values, {get(target, slot, receiver) {
        if (/^(0|[1-9][0-9]*)$/.test(String(slot))) event(`index:${key}`);
        return Reflect.get(target, slot, receiver);
      }});
      if (key === group.input) Object.defineProperty(values, 'map', {get() {
        event(`map-get:${key}`);
        return poison(function(callback) {
          assert.equal(this, array); event(`map-call:${key}`);
          return Reflect.apply(Array.prototype.map, array, [(item, index) => {
            if (mode === 'deferred') { pending.push({callback, item, index}); return undefined; }
            event(`callback:${key}`);
            const returned = Reflect.apply(callback, {unrelated: true}, [item, index, array]);
            assert.equal(returned, undefined); return returned;
          }]);
        });
      }});
      Object.defineProperty(result, key, {get() {
        event(`read:${key}`);
        if (mode === 'live' && key === group.input && tag === 'original')
          dependency.setExport('HomeConstants', constants('live'));
        if (key !== 'SECTION_PRODUCTS_COMMENTS') return array;
        const value = Object.freeze({tag, key}); identities.set(value, [tag, key, undefined]); return value;
      }});
    }
    return result;
  }
  dependency = new vm.SyntheticModule(['HomeConstants'], function() {
    this.setExport('HomeConstants', constants('original'));
  }, {context});
  const modules = texts.map(text => new vm.SourceTextModule(text, {context}));
  for (const [index, config] of configs.entries()) {
    await modules[index + 2].link(name => { assert.equal(name, specifier('constants/HomeConstants')); return dependency; });
    await modules[index].link(name => { assert.equal(name, specifier(`model/${config.model}`)); return modules[index + 2]; });
    assert.throws(() => modules[index].namespace[config.view], {name: 'ReferenceError'});
    await modules[index].evaluate();
    const Model = modules[index + 2].namespace[config.model];
    const init = Model.prototype.initData;
    Object.defineProperty(Model.prototype, 'initData', {configurable: true, get() {
      models.push({value: this, config}); event(`init-get:${config.model}`);
      return poison(function() { event(`init-call:${config.model}`); return Reflect.apply(init, this, []); });
    }});
  }
  const snapshot = (list, config) => {
    const Item = modules[configs.indexOf(config) + 2].namespace[config.item];
    return Array.from(list, item => {
      assert.ok(item instanceof Item);
      return config.getters.map((getter, index) => {
        const value = item[getter]();
        if (!value) return null;
        assert.ok(identities.has(value), 'constructor/getter must preserve original resource identity');
        const descriptor = Object.getOwnPropertyDescriptor(item, config.fields[index]);
        assert.equal(descriptor.value, value);
        assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
        return identities.get(value);
      });
    });
  };
  let thrown = false;
  try {
    for (const [index, config] of configs.entries()) {
      const C = modules[index].namespace[config.view];
      const first = new C(), second = new C(); views.push({first, second, config});
      assert.notEqual(first[config.field], second[config.field]);
      for (const group of config.groups) {
        const list = first[config.field][group.field];
        assert.notEqual(list, second[config.field][group.field]);
        const result = first[group.method](2);
        if (group.slice) {
          assert.notEqual(result, list); assert.equal(result.length, Math.min(list.length, 2));
          for (const [index, item] of result.entries()) assert.equal(item, list[index]);
        }
        else assert.equal(result, list);
      }
    }
    if (mode === 'late-live' || mode === 'deferred') dependency.setExport('HomeConstants', constants('later'));
    if (mode === 'deferred') {
      assert.ok(models.every(({value, config}) => config.groups.every(group => value[group.field].length === 0)));
      // All four actual instances retain their own receiver across delayed,
      // interleaved map callbacks and a changed live constants binding.
      for (const {callback, item, index} of pending.reverse()) {
        event('deferred:callback');
        assert.equal(Reflect.apply(callback, {wrong: true}, [item, index]), undefined);
      }
    } else for (const {value} of models.slice()) value.initData();
  } catch (error) { assert.equal(error, failure); thrown = true; }
  assert.equal(thrown, throwAt !== null);
  const state = models.map(({value, config}) => config.groups.map(group => snapshot(value[group.field], config)));
  const results = views.map(({first, config}) => config.groups.map(group => {
    const list = first[config.field][group.field], result = first[group.method](2);
    if (group.slice) {
      assert.notEqual(result, list);
      for (const [index, item] of result.entries()) assert.equal(item, list[index]);
    } else assert.equal(result, list);
    return snapshot(result, config);
  }));
  return {trace, points: [...points], state, results, thrown};
}

let boundaryCases = 0, connectedCases = 0;
const counts = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity, 2, -2, .75, '2', 2n, Symbol('count'), 'coercible', 'coercion-throw'];
for (const [index, config] of configs.entries()) {
  style(sources[index], config);
  for (const group of config.groups) for (const size of [0, 1, 4, 200, 201])
    for (const count of group.slice ? counts : [undefined])
      for (const mode of ['ordinary', 'before-construct', 'constructor-live', 'field-switch', 'method-switch', 'slice-switch']) {
        const expected = await boundary(paired[index], config, group, size, count, mode);
        assert.deepEqual(await boundary(sources[index], config, group, size, count, mode), expected); ++boundaryCases;
        // Error injections cover every observed boundary without multiplying
        // the large/count matrix: the four-element case exposes all protocols.
        if (size === 4 && (group.slice ? count === 2 : true) && mode === 'ordinary') for (const point of expected.points) {
          assert.deepEqual(await boundary(sources[index], config, group, size, count, mode, point),
            await boundary(paired[index], config, group, size, count, mode, point)); ++boundaryCases;
        }
      }
  for (const abrupt of [false, true]) for (const alternate of [false, true])
    await assignment(sources[index], config, abrupt, alternate);
}
for (const size of [0, 1, 4, 200, 201]) for (const mode of ['ordinary', 'sparse', 'live', 'late-live', 'deferred']) {
  const expected = await connected(paired, size, mode);
  assert.deepEqual(await connected(sources, size, mode), expected); ++connectedCases;
  for (const point of expected.points) {
    assert.deepEqual(await connected(sources, size, mode, point), await connected(paired, size, mode, point)); ++connectedCases;
    if (size === 4 && (mode === 'ordinary' || mode === 'deferred')) {
      for (const occurrence of [2, 3]) if (expected.trace.filter(name => name === point).length >= occurrence) {
        assert.deepEqual(await connected(sources, size, mode, point, occurrence),
          await connected(paired, size, mode, point, occurrence)); ++connectedCases;
      }
    }
  }
}
const mutations = [
  {index: 0, name: 'slice end lost', change: s => s.replace('.slice(0, count)', '.slice(0, void 0)'),
   check: ss => boundary(ss[0], configs[0], configs[0].groups[1], 4, 2, 'ordinary')},
  {index: 0, name: 'acquired receiver replaced', change: s => s.replace('this.iconInfoModel.getWelfareList()', '(0, this.iconInfoModel.getWelfareList)()'),
   check: ss => boundary(ss[0], configs[0], configs[0].groups[1], 4, 2, 'ordinary')},
  {index: 1, name: 'setter bypassed', change: s => s.replace('this.model = new SectionModel()',
    'Object.defineProperty(this, "model", {value: new SectionModel(), writable: true, enumerable: true, configurable: true})'),
   check: ss => assignment(ss[1], configs[1], false, false)},
  {index: 2, name: 'model initialization removed', change: s => s.replace('this.initData();', 'void 0;'),
   check: ss => connected(ss, 4, 'ordinary')},
  {index: 3, name: 'model argument positions exchanged', change: s => s.replace('HomeConstants.FLASH_SALE_PRODUCT_INFO[index], HomeConstants.FLASH_SALE_PRODUCT_ORIGINAL_PRICES[index]',
    'HomeConstants.FLASH_SALE_PRODUCT_ORIGINAL_PRICES[index], HomeConstants.FLASH_SALE_PRODUCT_INFO[index]'),
   check: ss => connected(ss, 4, 'deferred')},
];
for (const {index, name, change, check} of mutations) {
  const changed = sources.slice(); changed[index] = change(changed[index]); assert.notEqual(changed[index], sources[index]);
  new vm.SourceTextModule(changed[index]); // A syntax failure is not behavioral detection.
  let detected = false;
  try { assert.deepEqual(await check(changed), await check(sources)); }
  catch (error) { if (!(error instanceof assert.AssertionError)) throw error; detected = true; }
  assert.equal(detected, true, name);
}
console.log(JSON.stringify({wholeViewRecords: 2, connectedRecords: 4, boundaryCases, connectedCases, assignmentCases: 8, mutations: mutations.length}));
