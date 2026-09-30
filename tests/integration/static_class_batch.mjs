import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
const [tool, root] = process.argv.slice(2);
// Evaluation fixtures only. Production receives the ABC path, never these sources.
const cases = [
  ['Immersive', 7167, 'Immersive_src/commons/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 19020, 'src/base/src/main/ets/constants/BreakpointConstants.ts'],
  ['MultiCommunityApplication', 19442, 'src/base/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 20830, 'src/detail/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 31170, 'src/hot/src/main/ets/constants/CommonConstants.ts'],
  ['MultiCommunityApplication', 47487, 'src/rank/src/main/ets/constants/CommonConstants.ts'],
  ['pasteboard', 4182, 'pasteboard_src/src/main/ets/common/CommonConstants.ts'],
];
async function load(source) {
  const module = new vm.SourceTextModule(source);
  await module.link(() => { throw new Error('unexpected import'); });
  const names = Reflect.ownKeys(module.namespace).filter(key => typeof key === 'string');
  for (const name of names) assert.throws(() => module.namespace[name], { name: 'ReferenceError' });
  await module.evaluate();
  return module.namespace;
}
function same(a, b) {
  if (b === null || typeof b !== 'object') { assert.ok(Object.is(a, b)); return; }
  assert.equal(Array.isArray(a), Array.isArray(b));
  assert.deepEqual(Reflect.ownKeys(a), Reflect.ownKeys(b));
  for (const key of Reflect.ownKeys(b)) {
    const x = Object.getOwnPropertyDescriptor(a, key), y = Object.getOwnPropertyDescriptor(b, key);
    for (const flag of ['writable', 'enumerable', 'configurable']) assert.equal(x[flag], y[flag]);
    same(x.value, y.value);
  }
}
function separate(a, b) {
  if (a === null || typeof a !== 'object') return;
  assert.notEqual(a, b);
  for (const key of Object.keys(a)) separate(a[key], b[key]);
}
const temporary = mkdtempSync(join(tmpdir(), 'abcd-static-class-batch-'));
try {
  const manifests = new Map();
  for (const app of new Set(cases.map(item => item[0]))) {
    const destination = join(temporary, app);
    const run = spawnSync(tool, [join(root, app, 'modules.abc'), destination, '--value-source'],
      { encoding: 'utf8', timeout: 60_000 });
    assert.equal(run.status, 0, run.stderr + run.stdout);
    manifests.set(app, JSON.parse(readFileSync(join(destination, 'manifest.json'), 'utf8')));
  }
  for (const [app, entity, paired] of cases) {
    const record = manifests.get(app).records.find(row => row.entity === entity);
    assert.equal(record.source_complete, true);
    assert.equal(record.legacy_writer_used, false);
    const source = readFileSync(join(temporary, app, record.source), 'utf8');
    assert.doesNotMatch(source, /INCOMPLETE/);
    const actual = await load(source), other = await load(source);
    const expected = await load(stripTypeScriptTypes(readFileSync(join(root, app, paired), 'utf8')));
    assert.deepEqual(Object.keys(actual), Object.keys(expected));
    for (const name of Object.keys(expected)) {
      const a = actual[name], b = expected[name];
      assert.equal(a.name, b.name); assert.equal(a.length, b.length);
      assert.deepEqual(Reflect.ownKeys(a), Reflect.ownKeys(b));
      for (const key of Reflect.ownKeys(b)) {
        const x = Object.getOwnPropertyDescriptor(a, key), y = Object.getOwnPropertyDescriptor(b, key);
        for (const flag of ['writable', 'enumerable', 'configurable']) assert.equal(x[flag], y[flag]);
        if (key !== 'prototype') { same(x.value, y.value); separate(x.value, other[name][key]); }
      }
      assert.deepEqual(Reflect.ownKeys(a.prototype), Reflect.ownKeys(b.prototype));
      assert.equal(a.prototype.constructor, a);
      assert.equal(Object.getPrototypeOf(new a()), a.prototype);
    }
    if (app === 'pasteboard') {
      const values = [{value: 1}, {value: 2}, {value: 3}];
      const a = new actual.BreakpointType(...values), b = new expected.BreakpointType(...values);
      for (const key of ['sm', 'md', 'lg', 'unknown', undefined, {toString() { throw new Error('coercion'); }}])
        assert.equal(a.GetValue(key), b.GetValue(key));
      a.sm = b.sm = {changed: true}; assert.equal(a.GetValue('sm'), b.GetValue('sm'));
    }
    console.log(`${app} record=${entity} paired class exports passed`);
  }
} finally { rmSync(temporary, { recursive: true, force: true }); }
