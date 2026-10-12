import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
const [tool, root] = process.argv.slice(2);
const cases = [
  [16752, 'IconInfoModel', 'IconInfo', ['getCategoryList', 'getWelfareList'], ['getInfo', 'getIcon']],
  [17175, 'RecommendModel', 'RecommendProduct', ['getRecommendProductList'], ['getImage', 'getIsLive']],
  [17544, 'SectionModel', 'SectionProduct', ['getSectionProductList', 'getFlashSaleProductList'],
    ['getProductImg', 'getTitle', 'getPrice', 'getFeature', 'getComments', 'getInfo', 'getOriginalPrice']],
];
const snapshot = value => value === null || typeof value !== 'object' ? value :
  Reflect.ownKeys(value).map(key => {
    const d = Object.getOwnPropertyDescriptor(value, key);
    return [key, d.writable, d.enumerable, d.configurable, snapshot(d.value)];
  });
async function exercise(source, constantsSource, breakpointSource, test) {
  const [, modelName, itemName, lists, getters] = test;
  const context = vm.createContext({});
  // Test-only hooks mutate the actual lexical constructor binding. Production
  // never receives the paired sources or these evaluation hooks.
  const model = new vm.SourceTextModule(source + `\nexport function replace(next) { ${itemName} = next; }\nexport function readItem() { return ${itemName}; }`, {context});
  const constants = new vm.SourceTextModule(constantsSource, {context});
  const breakpoint = new vm.SourceTextModule(breakpointSource, {context});
  await model.link(specifier => {
    if (specifier === '@normalized:N&&&shopping/src/main/ets/constants/HomeConstants&1.0.0') return constants;
    assert.equal(specifier, '@normalized:N&&&commons/Index&1.0.0'); return breakpoint;
  });
  // Function declarations are initialized during linking, while class/let
  // bindings are still in TDZ. Both independently built modules must throw.
  assert.throws(() => model.namespace.readItem(), {name: 'ReferenceError'});
  await model.evaluate();
  assert.equal(model.namespace.readItem(), model.namespace[itemName]);
  const trace = [];
  for (const key of Object.keys(constants.namespace.HomeConstants)) {
    const value = constants.namespace.HomeConstants[key];
    Object.defineProperty(constants.namespace.HomeConstants, key, {configurable: true, get() { trace.push(key); return value; }});
  }
  const Model = model.namespace[modelName], Item = model.namespace[itemName];
  const argumentsByGetter = getters.map((_, i) => ({ordinal: i}));
  const explicit = new Item(...argumentsByGetter);
  getters.forEach((name, i) => assert.equal(explicit[name](), argumentsByGetter[i]));
  const explicitFields = snapshot(explicit);
  const receiverReads = [], markerValue = {};
  const receiver = new Proxy({}, {get(_target, key) { receiverReads.push(key); return markerValue; }});
  for (const name of getters) assert.equal(Item.prototype[name].call(receiver), markerValue);
  const getterFailure = new Error('receiver getter sentinel');
  const throwingReceiver = new Proxy({}, {get() { throw getterFailure; }});
  for (const name of getters) assert.throws(() => Item.prototype[name].call(throwingReceiver), error => error === getterFailure);
  const instance = new Model();
  const arrays = lists.map(name => instance[name]());
  const initialTrace = [...trace];
  const values = arrays.map(array => Array.from(array, item => {
    assert.equal(Object.getPrototypeOf(item), Item.prototype);
    return getters.map(name => snapshot(item[name]()));
  }));
  instance.initData();
  lists.forEach((name, i) => {
    assert.equal(instance[name](), arrays[i]);
    assert.equal(arrays[i].length, values[i].length * 2);
    assert.notEqual(new Model()[name](), arrays[i]);
  });
  const args = [], marker = {};
  function Replacement(...input) { assert.ok(new.target); args.push(input.map(snapshot)); return marker; }
  model.namespace.replace(Replacement);
  const changed = new Model();
  lists.forEach(name => assert.ok(changed[name]().every(item => item === marker)));
  const failure = new Error('constructor sentinel');
  model.namespace.replace(function() { throw failure; });
  assert.throws(() => new Model(), error => error === failure);
  return {values, explicitFields, receiverReads, trace: initialTrace, args, name: Model.name, length: Model.length};
}
const temporary = mkdtempSync(join(tmpdir(), 'abcd-local-models-'));
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8', timeout: 60000});
  assert.equal(run.status, 0, run.stderr + run.stdout);
  const records = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records;
  const generated = entity => {
    const row = records.find(row => row.entity === entity);
    assert.equal(row.source_complete, true, `record ${entity}`); assert.equal(row.legacy_writer_used, undefined);
    return readFileSync(join(output, row.source), 'utf8');
  };
  const paired = path => stripTypeScriptTypes(readFileSync(join(root, 'Immersive_src', path), 'utf8'));
  for (const test of cases) {
    const actual = await exercise(generated(test[0]), generated(16247), generated(7395), test);
    const expected = await exercise(paired(`shopping/src/main/ets/model/${test[1]}.ts`),
      paired('shopping/src/main/ets/constants/HomeConstants.ts'), paired('commons/src/main/ets/utils/Breakpoint.ts'), test);
    assert.deepEqual(actual, expected, test[1]);
  }
  console.log('Three local-constructor model records passed independent paired execution');
} finally { rmSync(temporary, {recursive: true, force: true}); }
