import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { stripTypeScriptTypes } from 'node:module';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import {verifyTabBarRecords} from './tabbar_records.mjs';
const [tool, root] = process.argv.slice(2);
const cases = [
  ['Immersive', 28887, 'HomeViewModel', 'HomeModel', 'homeModel', ['getTabBarList']],
  ['Immersive', 29119, 'IconInfoViewModel', 'IconInfoModel', 'iconInfoModel', ['getCategoryList', 'getWelfareList']],
  ['Immersive', 29382, 'RecommendProductViewModel', 'RecommendModel', 'model', ['getRecommendProductList']],
  ['Immersive', 29619, 'SectionViewModel', 'SectionModel', 'model', ['getSectionProductList', 'getFlashSaleProductList']],
  ['MultiCommunityApplication', 44106, 'TabBarViewModel'],
  ['MultiCommunityApplication', 30839, 'CardItem'],
];
const paths = new Map([
  [28887, 'Immersive_src/shopping/src/main/ets/viewmodel/HomeViewModel.ts'],
  [29119, 'Immersive_src/shopping/src/main/ets/viewmodel/IconInfoViewModel.ts'],
  [29382, 'Immersive_src/shopping/src/main/ets/viewmodel/RecommendViewModel.ts'],
  [29619, 'Immersive_src/shopping/src/main/ets/viewmodel/SectionViewModel.ts'],
  [44106, 'src/phone/src/main/ets/viewmodel/TabBarViewModel.ts'],
  [30839, 'src/detail/src/main/ets/viewmodel/CardViewModel.ts'],
]);
const hashes = new Map([
  ['Immersive', '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a'],
  ['MultiCommunityApplication', '45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8'],
]);
async function load(source, exports) {
  const context = vm.createContext({});
  const dependency = new vm.SyntheticModule(Object.keys(exports), function () {
    for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
  }, { context });
  const module = new vm.SourceTextModule(source, { context });
  await module.link(specifier => {
    assert.ok(specifier.startsWith('@normalized:N&&&'));
    return dependency;
  });
  for (const name of Reflect.ownKeys(module.namespace).filter(key => typeof key === 'string'))
    assert.throws(() => module.namespace[name], { name: 'ReferenceError' });
  await module.evaluate();
  return { namespace: module.namespace, dependency };
}
async function models(source, name, dependencyName, field, methods) {
  const trace = [], values = [{id: 1}, {id: 2}, {id: 3}];
  class Model {
    constructor() { trace.push('construct'); }
  }
  for (const method of methods) Model.prototype[method] = function () {
    assert.equal(this, instance[field]); trace.push(method); return values;
  };
  const loaded = await load(source, { [dependencyName]: Model });
  const C = loaded.namespace[name];
  const prototype = Object.getOwnPropertyDescriptor(C, 'prototype');
  assert.equal(prototype.writable, false); assert.equal(prototype.configurable, false);
  assert.equal(prototype.enumerable, false); assert.equal(prototype.value, C.prototype);
  assert.throws(() => Object.defineProperty(C, 'prototype', {get() {throw new Error('prototype getter');}}),
    {name: 'TypeError'});
  const instance = new C();
  assert.equal(instance[field].constructor, Model);
  for (const method of methods) {
    const sliced = ['getWelfareList', 'getSectionProductList', 'getFlashSaleProductList'].includes(method);
    const body = C.prototype[method].toString();
    assert.ok(!body.includes('$value') && !body.includes('.call('), `${name}.${method} must use native member syntax`);
    assert.ok(body.includes(`.${field}`) && body.includes(`.${method}(`));
    if (sliced) assert.ok(body.includes('.slice('));
    for (const count of [undefined, -1, 0, 1, 9, NaN, { valueOf() { trace.push('count'); return 2; } }]) {
      const result = instance[method](count);
      if (sliced) {
        assert.notEqual(result, values);
        // Avoid coercing the instrumented count twice in the independent oracle.
        const end = typeof count === 'object' ? 2 : count;
        assert.deepEqual(result, values.slice(0, end));
      } else assert.equal(result, values);
    }
    const original = instance[field];
    Object.defineProperty(instance, field, { configurable: true, get() { trace.push('receiver'); return original; } });
    original[method] = function () { assert.equal(this, original); trace.push('throw'); throw new Error('method sentinel'); };
    assert.throws(() => instance[method](1), { message: 'method sentinel' });
    Object.defineProperty(instance, field, { configurable: true, writable: true, value: original });
    // Paired code and recovered code each run the same independent host objects.
    // Changing the field/getter after reference acquisition must not re-read it.
    for (const throwAt of [null, 'field', 'getter', 'call', ...(sliced ? ['slice-getter', 'slice-call', 'count'] : [])]) {
      const sentinel = {}, result = {}, list = {};
      let reads = 0;
      const event = name => { trace.push(name); if (throwAt === name) throw sentinel; };
      const count = {valueOf() { event('count'); return 2; }};
      Object.defineProperty(instance, field, {configurable: true, get() {
        event('field'); return ++reads === 1 ? original : {};
      }});
      Object.defineProperty(original, method, {configurable: true, get() {
        event('getter');
        const fn = function() { assert.equal(this, original); event('call'); return sliced ? list : result; };
        Object.defineProperty(fn, 'call', {get() { throw new Error('unexpected function.call'); }});
        Object.defineProperty(original, method, {configurable: true, value() { throw new Error('repeated getter'); }});
        return fn;
      }});
      Object.defineProperty(list, 'slice', {get() {
        event('slice-getter');
        const fn = function(start, end) {
          assert.equal(this, list); assert.equal(start, 0); assert.equal(end, count);
          event('slice-call'); values.slice(start, end); return result;
        };
        Object.defineProperty(fn, 'call', {get() { throw new Error('unexpected slice.call'); }});
        return fn;
      }});
      if (throwAt) assert.throws(() => instance[method](count), error => error === sentinel);
      else assert.equal(instance[method](count), result);
      Object.defineProperty(instance, field, {configurable: true, writable: true, value: original});
      delete original[method];
    }
  }
  loaded.dependency.setExport(dependencyName, class { constructor() { throw new Error('live constructor'); } });
  assert.throws(() => new C(), { message: 'live constructor' });
  return { trace, name: C.name, length: C.length, methods: Reflect.ownKeys(C.prototype), fields: Reflect.ownKeys(instance) };
}
async function tabs(source) {
  const first = [{id: 1}], second = [{id: 2}];
  const { namespace, dependency } = await load(source, { TabBarsInfo: first });
  const C = namespace.TabBarViewModel, old = new C();
  assert.equal(old.getTabList(), first);
  dependency.setExport('TabBarsInfo', second);
  assert.equal(old.getTabList(), first);
  assert.equal(new C().getTabList(), second);
  first.push({id: 3}); assert.equal(old.getTabList().length, 2);
  return { name: C.name, length: C.length, fields: Reflect.ownKeys(old) };
}
async function cards(source) {
  const trace = [], pictures = [[{pic: 0}], [{pic: 1}]], comments = [{id: 1}, {id: 2}];
  class CommentItem { constructor(item) { trace.push(`comment:${item.id}`); this.item = item; } }
  const { namespace, dependency } = await load(source, {
    COMMENT_LIST: comments, CommentItem, PICTURE_ARRAY_LIST: pictures,
  });
  const C = namespace.CardItem;
  const card = {};
  for (const key of ['icon', 'name', 'authorType', 'content'])
    Object.defineProperty(card, key, { get() { trace.push(key); return key; } });
  const value = new C(card, { valueOf() { trace.push('index'); return 3; } });
  assert.equal(value.pictureArray, pictures[1]);
  assert.deepEqual(Array.from(value.commentList, item => item.item), comments);
  assert.ok(value.commentList.every(item => item instanceof CommentItem));
  const other = new C(card, 0);
  assert.notEqual(value.commentList, other.commentList);
  dependency.setExport('CommentItem', class { constructor() { throw new Error('comment sentinel'); } });
  assert.throws(() => new C(card, 0), { message: 'comment sentinel' });
  for (const key of ['icon', 'name', 'authorType', 'content']) assert.equal(value[key], key);
  // Uninitialized TS field declarations survive Node's type stripper, whereas
  // their ABC storage order is compiler-dependent. Compare field descriptors
  // by name here; this test does not certify their enumeration order.
  const fields = Object.keys(value).sort().map(key => {
    const d = Object.getOwnPropertyDescriptor(value, key);
    return [key, d.enumerable, d.configurable, d.writable];
  });
  return { trace, fields, name: C.name, length: C.length };
}
const temporary = mkdtempSync(join(tmpdir(), 'abcd-viewmodel-batch-'));
try {
  const manifests = new Map();
  for (const app of new Set(cases.map(item => item[0]))) {
    const destination = join(temporary, app);
    const abc = join(root, app, 'modules.abc');
    const hash = createHash('sha256').update(readFileSync(abc)).digest('hex');
    assert.equal(hash, hashes.get(app));
    const run = spawnSync(tool, [abc, destination, '--value-source'],
      { encoding: 'utf8', timeout: 60_000 });
    assert.equal(run.status, 0, run.stderr + run.stdout);
    const manifest = JSON.parse(readFileSync(join(destination, 'manifest.json'), 'utf8'));
    assert.equal(manifest.sha256, hash); assert.equal(manifest.source_route, 'value_tree');
    manifests.set(app, manifest);
  }
  for (const [app, entity, name, dependency, field, methods] of cases) {
    const record = manifests.get(app).records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    assert.equal(record.source_route, 'value_tree');
    const source = readFileSync(join(temporary, app, record.source), 'utf8');
    // Safe class-self scopes use native declarations. A discarded own
    // prototype read must not force a carrier or naming wrapper.
    assert.match(source, new RegExp(`\\bclass ${name} \\{`));
    assert.doesNotMatch(source, /^\$value\d+\.prototype;$/m);
    const paired = stripTypeScriptTypes(readFileSync(join(root, app, paths.get(entity)), 'utf8'));
    const exercise = entity === 44106 ? tabs : entity === 30839 ? cards : s => models(s, name, dependency, field, methods);
    assert.deepEqual(await exercise(source), await exercise(paired));
    console.log(`${app} record=${entity} paired viewmodel behavior passed`);
  }
  const app = 'MultiCommunityApplication';
  const sources = [44106, 41409].map(entity => {
    const record = manifests.get(app).records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    assert.equal(record.source_route, 'value_tree');
    return readFileSync(join(temporary, app, record.source), 'utf8');
  });
  const references = ['viewmodel/TabBarViewModel.ts', 'model/TabBarModel.ts'].map(path =>
    stripTypeScriptTypes(readFileSync(join(root, app, 'src/phone/src/main/ets', path), 'utf8')));
  console.log(JSON.stringify(await verifyTabBarRecords(...sources, ...references)));
} finally { rmSync(temporary, { recursive: true, force: true }); }
