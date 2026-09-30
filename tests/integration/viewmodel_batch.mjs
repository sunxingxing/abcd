import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
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
  const instance = new C();
  assert.equal(instance[field].constructor, Model);
  for (const method of methods) {
    const sliced = ['getWelfareList', 'getSectionProductList', 'getFlashSaleProductList'].includes(method);
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
    const run = spawnSync(tool, [join(root, app, 'modules.abc'), destination, '--value-source'],
      { encoding: 'utf8', timeout: 60_000 });
    assert.equal(run.status, 0, run.stderr + run.stdout);
    manifests.set(app, JSON.parse(readFileSync(join(destination, 'manifest.json'), 'utf8')));
  }
  for (const [app, entity, name, dependency, field, methods] of cases) {
    const record = manifests.get(app).records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    const source = readFileSync(join(temporary, app, record.source), 'utf8');
    const paired = stripTypeScriptTypes(readFileSync(join(root, app, paths.get(entity)), 'utf8'));
    const exercise = entity === 44106 ? tabs : entity === 30839 ? cards : s => models(s, name, dependency, field, methods);
    assert.deepEqual(await exercise(source), await exercise(paired));
    console.log(`${app} record=${entity} paired viewmodel behavior passed`);
  }
} finally { rmSync(temporary, { recursive: true, force: true }); }
