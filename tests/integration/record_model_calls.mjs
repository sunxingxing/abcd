import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [iconPath, sectionPath, iconPairedPath, sectionPairedPath] = process.argv.slice(2);
// Only the independent test reads paired TS, after ABC-only generation finishes.
const sources = await Promise.all([iconPath, sectionPath].map(path => readFile(path, 'utf8')));
const paired = await Promise.all([iconPairedPath, sectionPairedPath].map(async path =>
  stripTypeScriptTypes(await readFile(path, 'utf8'))));
const specifier = '@normalized:N&&&shopping/src/main/ets/constants/HomeConstants&1.0.0';
const models = [
  {name: 'IconInfoModel', item: 'IconInfo', fields: ['info', 'icon'],
   getters: ['getInfo', 'getIcon'], groups: [
    {field: 'categoryList', getter: 'getCategoryList', input: 'CATEGORY_INFO_LIST',
     args: ['CATEGORY_ICON_LIST']},
    {field: 'welfareList', getter: 'getWelfareList', input: 'WELFARE_PRICE_LIST',
     args: ['WELFARE_ICON_LIST']},
   ]},
  {name: 'SectionModel', item: 'SectionProduct',
   fields: ['productImg', 'title', 'price', 'feature', 'comments', 'info', 'originalPrice'],
   getters: ['getProductImg', 'getTitle', 'getPrice', 'getFeature', 'getComments', 'getInfo', 'getOriginalPrice'],
   groups: [
    {field: 'sectionProductList', getter: 'getSectionProductList', input: 'SECTION_PRODUCT_IMAGES',
     args: ['SECTION_PRODUCTS_TITLES', 'SECTION_PRODUCTS_PRICES', 'SECTION_PRODUCTS_FEATURES',
      'SECTION_PRODUCTS_COMMENTS', 'SECTION_PRODUCTS_INFO']},
    {field: 'flashSaleProductList', getter: 'getFlashSaleProductList', input: 'FLASH_SALE_PRODUCT_IMAGES',
     args: ['FLASH_SALE_PRODUCT_TITLES', 'FLASH_SALE_PRODUCT_PRICES', 'FLASH_SALE_PRODUCT_FEATURES',
      'FLASH_SALE_PRODUCT_INFO', 'FLASH_SALE_PRODUCT_ORIGINAL_PRICES']},
   ]},
];
const scalar = 'SECTION_PRODUCTS_COMMENTS';
async function load(text, config, constants) {
  const context = vm.createContext({});
  const dependency = new vm.SyntheticModule(['HomeConstants'], function() {
    this.setExport('HomeConstants', constants);
  }, {context});
  const module = new vm.SourceTextModule(text, {context});
  await module.link(name => { assert.equal(name, specifier); return dependency; });
  assert.throws(() => module.namespace[config.name], {name: 'ReferenceError'});
  assert.throws(() => module.namespace[config.item], {name: 'ReferenceError'});
  await module.evaluate();
  assert.deepEqual(Object.keys(module.namespace).sort(), [config.name, config.item].sort());
  return {C: module.namespace[config.name], Item: module.namespace[config.item], dependency};
}
function style(text, config) {
  assert.match(text, /constructor\(\)/);
  assert.match(text, /\.map\(/);
  assert.match(text, /\.push\(new /);
  assert.match(text, /=>/);
  for (const group of config.groups) {
    assert.match(text, new RegExp(`this\\.${group.field}\\s*=\\s*\\[\\]`));
    assert.match(text, new RegExp(`${group.getter}\\(\\)`));
  }
  assert.doesNotMatch(text, /INCOMPLETE|UNRECOVERED|Reflect\.construct|\.call\(|\bswitch\s*\(/);
}

async function observe(text, config, count, mode, throwAt = null, assignmentFields = false) {
  const trace = [], points = new Set(), sentinel = {}, retained = [], pending = [];
  let captured, dependency;
  function event(name, detail = '') {
    points.add(name); trace.push(detail ? `${name}:${detail}` : name);
    if (name === throwAt) throw sentinel;
  }
  function constants(tag, switchBinding) {
    const result = {};
    for (const group of config.groups) {
      for (const key of [group.input, ...group.args]) {
        const input = key === group.input;
        const values = Array.from({length: count}, (_, i) => Object.freeze({tag, key, i}));
        if (input && mode === 'sparse' && count > 1) delete values[1];
        let array;
        array = new Proxy(values, {get(target, keyName, receiver) {
          if (typeof keyName === 'string' && /^(0|[1-9][0-9]*)$/.test(keyName)) {
            event(`index:${key}`, keyName);
            if (mode === 'argument-live' && key === group.args[0] && tag === 'original')
              dependency.setExport('HomeConstants', constants('argument-live', false));
          }
          return Reflect.get(target, keyName, receiver);
        }});
        if (input) Object.defineProperty(values, 'map', {get() {
          event(`map-get:${key}`);
          const fn = function(callback) {
            assert.equal(this, array); event(`map-call:${key}`);
            return Array.prototype.map.call(array, (item, index) => {
              if (mode === 'deferred') { pending.push({callback, item, index, key}); return undefined; }
              event(`callback:${key}`, String(index));
              const value = Reflect.apply(callback, {unrelated: true}, [item, index, array]);
              assert.equal(value, undefined); return value;
            });
          };
          Object.defineProperty(fn, 'call', {get() { throw new Error('map.call read'); }});
          return fn;
        }});
        Object.defineProperty(result, key, {get() {
          event(`read:${key}`);
          if (input && switchBinding) dependency.setExport('HomeConstants', constants('live', false));
          return key === scalar ? Object.freeze({tag, key}) : array;
        }});
      }
    }
    return result;
  }
  const loaded = await load(text, config, constants('original', mode === 'live'));
  const {C, Item} = loaded; dependency = loaded.dependency;
  assert.equal(C.name, config.name); assert.equal(C.length, 0);
  assert.equal(Item.name, config.item); assert.equal(Item.length, config.fields.length);
  assert.throws(() => C(), {name: 'TypeError'});
  const init = C.prototype.initData;
  assert.equal(init.length, 0);
  for (const getter of [...config.groups.map(group => group.getter), ...config.getters]) {
    const prototype = config.getters.includes(getter) ? Item.prototype : C.prototype;
    assert.equal(prototype[getter].length, 0); assert.equal(prototype[getter].name, getter);
  }
  function track(list, field) {
    retained.push(list);
    let replaced = false;
    Object.defineProperty(list, 'push', {configurable: true, get() {
      event(`push-get:${field}`);
      if (mode === 'replace-list' && !replaced) {
        replaced = true; captured[field] = track([] , field);
      }
      const fn = function(item) {
        assert.equal(this, list); event(`push-call:${field}`);
        assert.ok(item instanceof Item);
        return Array.prototype.push.call(list, item);
      };
      Object.defineProperty(fn, 'call', {get() {throw new Error('push.call read');}});
      return fn;
    }});
    return list;
  }
  Object.defineProperty(C.prototype, 'initData', {configurable: true, get() {
    captured = this;
    for (const group of config.groups) track(this[group.field], group.field);
    event('init-get');
    const fn = function() { assert.equal(this, captured); event('init-call'); return Reflect.apply(init, this, []); };
    Object.defineProperty(fn, 'call', {get() {throw new Error('init.call read');}});
    return fn;
  }});
  let thrown = false;
  try {
    new C();
    if (mode === 'deferred') {
      assert.ok(retained.every(list => list.length === 0));
      dependency.setExport('HomeConstants', constants('deferred-live', false));
      // Callbacks must retain their original receiver, then read its current
      // list when invoked; neither binding nor list is a lifetime snapshot.
      for (const group of config.groups) captured[group.field] = track([], group.field);
      for (const {callback, item, index, key} of pending) {
        event(`callback:${key}`, String(index));
        assert.equal(Reflect.apply(callback, {wrongReceiver: true}, [item, index]), undefined);
      }
    }
  } catch (error) { assert.equal(error, sentinel); thrown = true; }
  assert.equal(thrown, throwAt !== null);
  assert.ok(captured);
  for (const group of config.groups) {
    assert.equal(captured[group.getter](), captured[group.field]);
    const descriptor = Object.getOwnPropertyDescriptor(captured, group.field);
    assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
  }
  assert.deepEqual(Object.keys(captured), config.groups.map(group => group.field));
  const first = captured;
  if (!thrown && mode === 'ordinary') {
    const lists = config.groups.map(group => first[group.field]);
    Reflect.apply(init, first, []);
    for (const [i, group] of config.groups.entries()) {
      assert.equal(first[group.field], lists[i]); assert.equal(lists[i].length, count * 2);
    }
    new C();
    for (const [i, group] of config.groups.entries()) assert.notEqual(captured[group.field], lists[i]);
  }
  const snapshot = list => Array.from(list, item => {
    if (assignmentFields) {
      const initialized = config.fields.filter(field => field !== 'originalPrice');
      assert.deepEqual(Object.keys(item), initialized);
      for (const field of initialized) {
        const descriptor = Object.getOwnPropertyDescriptor(item, field);
        assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
      }
      if (config.fields.includes('originalPrice')) assert.equal(Object.hasOwn(item, 'originalPrice'), false);
    }
    return config.getters.map(getter => item[getter]());
  });
  // Node retains uninitialized TS fields as DefineField. Those declaration-only
  // own keys are not an oracle for pinned STOBJBYNAME; test assignment below.
  return {trace, points: [...points], lists: retained.map(snapshot),
    first: config.groups.map(group => snapshot(first[group.field])), thrown};
}

async function assignment(text, config, abrupt, alternate) {
  const {C, Item} = await load(text, config, {});
  let receiver;
  const trace = [], values = [], sentinel = {};
  const prototype = alternate ? {} : C.prototype;
  for (const group of config.groups) Object.defineProperty(prototype, group.field, {set(value) {
    receiver = this; trace.push(group.field); values.push(value);
    assert.ok(Array.isArray(value)); assert.equal(value.length, 0);
    if (abrupt) throw sentinel;
  }});
  Object.defineProperty(prototype, 'initData', {get() {
    assert.equal(this, receiver); trace.push('init-get');
    return function() { assert.equal(this, receiver); trace.push('init-call'); };
  }});
  function Target() {} Target.prototype = prototype;
  const construct = () => Reflect.construct(C, [], alternate ? Target : C);
  if (abrupt) assert.throws(construct, error => error === sentinel);
  else {
    const first = construct(), second = construct();
    assert.equal(Object.getPrototypeOf(first), prototype); assert.equal(receiver, second);
    assert.notEqual(first, second);
    for (const group of config.groups) assert.equal(Object.hasOwn(first, group.field), false);
    assert.notEqual(values[0], values[1]); assert.notEqual(values[0], values[2]);
  }
  const sequence = [...config.groups.map(group => group.field), 'init-get', 'init-call'];
  assert.deepEqual(trace, abrupt ? [config.groups[0].field] : [...sequence, ...sequence]);
  // Field setters receive the original constructor arguments, in source order.
  const itemPrototype = alternate ? {} : Item.prototype;
  const args = config.fields.map((field, index) => Object.freeze({field, index}));
  const writes = [];
  for (const field of config.fields) Object.defineProperty(itemPrototype, field, {set(value) {
    writes.push([field, value]); if (abrupt) throw sentinel;
  }});
  Target.prototype = itemPrototype;
  if (abrupt) assert.throws(() => Reflect.construct(Item, args, alternate ? Target : Item), error => error === sentinel);
  else {
    const item = Reflect.construct(Item, args, alternate ? Target : Item);
    assert.equal(Object.getPrototypeOf(item), itemPrototype);
    for (const field of config.fields) assert.equal(Object.hasOwn(item, field), false);
  }
  const expected = config.fields.map((field, index) => [field, args[index]]);
  assert.deepEqual(writes, abrupt ? expected.slice(0, 1) : expected);
}

async function itemValues(text, config) {
  const {Item} = await load(text, config, {});
  const values = [undefined, null, false, true, 0, -0, NaN, Infinity, 3n, 'value', Symbol('item'), {}, []];
  for (const value of values) {
    const args = config.fields.map(() => value), item = new Item(...args);
    assert.deepEqual(Object.keys(item), config.fields.filter(field => field !== 'originalPrice' || value !== undefined));
    for (const [index, field] of config.fields.entries()) {
      assert.ok(Object.is(item[config.getters[index]](), value));
      const initialized = field !== 'originalPrice' || value !== undefined;
      assert.equal(Object.hasOwn(item, field), initialized);
      if (initialized) {
        const descriptor = Object.getOwnPropertyDescriptor(item, field);
        assert.ok(Object.is(descriptor.value, value));
        assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, true, true]);
      }
    }
  }
  return values.length;
}
let pairedCases = 0, valueCases = 0;
for (const [index, config] of models.entries()) {
  style(sources[index], config);
  for (const count of [0, 1, 2, 200, 201]) for (const mode of ['ordinary', 'live', 'argument-live', 'replace-list', 'deferred', 'sparse']) {
    const baseline = await observe(paired[index], config, count, mode);
    assert.deepEqual(await observe(sources[index], config, count, mode, null, true), baseline); ++pairedCases;
    for (const point of baseline.points) {
      assert.deepEqual(await observe(sources[index], config, count, mode, point, true),
        await observe(paired[index], config, count, mode, point));
      ++pairedCases;
    }
  }
  for (const abrupt of [false, true]) for (const alternate of [false, true])
    await assignment(sources[index], config, abrupt, alternate);
  valueCases += await itemValues(sources[index], config);
}
const mutations = [
  {index: 0, name: 'initialization removed',
   change: text => text.replace('this.initData();', 'void 0;'),
   check: text => observe(text, models[0], 2, 'ordinary')},
  {index: 0, name: 'constructor argument identity lost',
   change: text => text.replace('this.info = info', 'this.info = null'),
   check: text => itemValues(text, models[0])},
  {index: 1, name: 'flash argument positions changed',
   change: text => text.replace('HomeConstants.FLASH_SALE_PRODUCT_INFO[index], HomeConstants.FLASH_SALE_PRODUCT_ORIGINAL_PRICES[index]',
     'HomeConstants.FLASH_SALE_PRODUCT_ORIGINAL_PRICES[index], HomeConstants.FLASH_SALE_PRODUCT_INFO[index]'),
   check: text => observe(text, models[1], 2, 'ordinary')},
  {index: 1, name: 'conditional property made unconditional',
   change: text => text.replace('if (originalPrice !== (void 0))', 'if (true)'),
   check: text => itemValues(text, models[1])},
  {index: 0, name: 'setter bypassed by own field',
   change: text => text.replace('this.categoryList = []',
     'Object.defineProperty(this, "categoryList", {value: [], writable: true, enumerable: true, configurable: true})'),
   check: text => assignment(text, models[0], false, false)},
];
for (const {index, name, change, check} of mutations) {
  const changed = change(sources[index]); assert.notEqual(changed, sources[index]);
  new vm.SourceTextModule(changed); // Syntax rejection is not semantic detection.
  let detected = false;
  try { assert.deepEqual(await check(changed), await check(sources[index])); }
  catch (error) { if (!(error instanceof assert.AssertionError)) throw error; detected = true; }
  assert.equal(detected, true, name);
}
console.log(JSON.stringify({wholeRecords: 2, pairedCases, assignmentCases: 8, valueCases, mutations: mutations.length}));
