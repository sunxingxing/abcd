import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
const [tool, root] = process.argv.slice(2);
const prefix = '@normalized:N&&&';
const records = [
  [7395, `${prefix}commons/Index&1.0.0`, 'commons/src/main/ets/utils/Breakpoint.ts'],
  [16247, `${prefix}shopping/src/main/ets/constants/HomeConstants&1.0.0`, 'shopping/src/main/ets/constants/HomeConstants.ts'],
  [16474, `${prefix}shopping/src/main/ets/model/HomeModel&1.0.0`, 'shopping/src/main/ets/model/HomeModel.ts'],
  [28887, 'viewmodel', 'shopping/src/main/ets/viewmodel/HomeViewModel.ts'],
];
function snapshot(value) {
  if (value === null || typeof value !== 'object') return value;
  return Reflect.ownKeys(value).map(key => {
    const d = Object.getOwnPropertyDescriptor(value, key);
    return [key, d.writable, d.enumerable, d.configurable, snapshot(d.value)];
  });
}
async function exercise(sources) {
  const context = vm.createContext({});
  const modules = new Map([...sources].map(([id, source]) => [id, new vm.SourceTextModule(source, {context, identifier: id})]));
  const rootModule = modules.get('viewmodel');
  await rootModule.link(specifier => { assert.ok(modules.has(specifier), specifier); return modules.get(specifier); });
  await rootModule.evaluate();
  const HomeModel = modules.get(records[2][1]).namespace.HomeModel;
  const constants = modules.get(records[1][1]).namespace.HomeConstants;
  const trace = [];
  const originals = new Map();
  for (const key of ['TAB_BAR_TEXT_LIST', 'TAB_BAR_ICON_LIST', 'TAB_BAR_ACTIVE_ICON_LIST']) {
    const value = constants[key]; originals.set(key, value);
    Object.defineProperty(constants, key, { configurable: true, get() { trace.push(key); return value; } });
  }
  const model = new HomeModel(), list = model.getTabBarList();
  assert.equal(list, model.getTabBarList());
  assert.equal(list.length, originals.get('TAB_BAR_TEXT_LIST').length);
  for (let i = 0; i < list.length; ++i) {
    assert.equal(list[i].text, originals.get('TAB_BAR_TEXT_LIST')[i]);
    assert.equal(list[i].icon, originals.get('TAB_BAR_ICON_LIST')[i]);
    assert.equal(list[i].activeIcon, originals.get('TAB_BAR_ACTIVE_ICON_LIST')[i]);
  }
  const initial = snapshot(list), initialTrace = [...trace];
  model.initTabBarList();
  assert.equal(model.getTabBarList(), list);
  assert.equal(list.length, originals.get('TAB_BAR_TEXT_LIST').length * 2);
  const other = new HomeModel();
  assert.notEqual(other.getTabBarList(), list);
  const view = new rootModule.namespace.HomeViewModel();
  assert.deepEqual(snapshot(view.getTabBarList()), initial);
  const sentinel = new Error('icon sentinel');
  Object.defineProperty(constants, 'TAB_BAR_ICON_LIST', { get() { throw sentinel; } });
  assert.throws(() => new HomeModel(), error => error === sentinel);
  return { initial, trace: initialTrace, repeated: snapshot(list), name: HomeModel.name, length: HomeModel.length };
}
const temporary = mkdtempSync(join(tmpdir(), 'abcd-home-model-'));
try {
  const destination = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), destination, '--value-source'], {encoding: 'utf8', timeout: 60_000});
  assert.equal(run.status, 0, run.stderr + run.stdout);
  const manifest = JSON.parse(readFileSync(join(destination, 'manifest.json'), 'utf8'));
  const actual = new Map(), expected = new Map();
  for (const [entity, specifier, paired] of records) {
    const record = manifest.records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true, `record ${entity}`);
    assert.equal(record.legacy_writer_used, false);
    actual.set(specifier, readFileSync(join(destination, record.source), 'utf8'));
    expected.set(specifier, stripTypeScriptTypes(readFileSync(join(root, 'Immersive_src', paired), 'utf8')));
  }
  assert.deepEqual(await exercise(actual), await exercise(expected));
  console.log('Generated Breakpoint/HomeConstants/HomeModel/HomeViewModel chain passed paired execution');
} finally { rmSync(temporary, {recursive: true, force: true}); }
