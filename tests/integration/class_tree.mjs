import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
const run = spawnSync(tool, [abc, '16948', '1'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
assert.match(run.stdout, /class\s*\{/);
assert.match(run.stdout, /constructor\(info, icon\)/);
assert.match(run.stdout, /getInfo\(\)/);
assert.match(run.stdout, /getIcon\(\)/);
// Check member/body nesting independently of the behavioral oracle.
assert.match(run.stdout, /\n  constructor\(info, icon\) \{\n    /);
assert.match(run.stdout, /\n  getInfo\(\) \{\n    return this\.info;\n  \}/);
assert.match(run.stdout, /\n  getIcon\(\) \{\n    return this\.icon;\n  \}/);
assert.doesNotMatch(run.stdout, /\$value|INCOMPLETE|function\s/);
async function load(source, key) {
  const module = new vm.SourceTextModule(source);
  await module.link(() => new vm.SourceTextModule('export const HomeConstants = {};'));
  await module.evaluate(); return module.namespace[key];
}
const actual = await load(`export const recovered = ${run.stdout};`, 'recovered');
const reference = await load(stripTypeScriptTypes(readFileSync(paired, 'utf8')), 'IconInfo');
function descriptor(object, key) {
  const d = Object.getOwnPropertyDescriptor(object, key);
  return {writable: d.writable, enumerable: d.enumerable, configurable: d.configurable};
}
function observe(C) {
  const info = {}, icon = {};
  const first = new C(info, icon), second = new C(icon, info);
  assert.notEqual(first, second);
  assert.equal(first.getInfo(), info); assert.equal(first.getIcon(), icon);
  assert.equal(second.getInfo(), icon); assert.equal(second.getIcon(), info);
  const changed = {};
  first.info = changed; assert.equal(first.getInfo(), changed);
  assert.equal(Reflect.apply(C.prototype.getInfo, {info}, []), info);
  assert.throws(() => Reflect.apply(C.prototype.getInfo, undefined, []), {name: 'TypeError'});
  assert.throws(() => new C.prototype.getInfo(), {name: 'TypeError'});
  assert.throws(() => Reflect.apply(C, {}, [info, icon]), {name: 'TypeError'});
  const sentinel = {};
  assert.throws(() => Reflect.apply(C.prototype.getIcon, {get icon() {throw sentinel;}}, []), e => e === sentinel);
  class Derived extends C {}
  const child = new Derived(info, icon);
  assert.equal(child.getInfo(), info); assert.equal(Object.getPrototypeOf(child), Derived.prototype);
  assert.equal(Object.getPrototypeOf(first), C.prototype);
  assert.equal(C.prototype.constructor, C);
  return {name: C.name, length: C.length, keys: Reflect.ownKeys(first),
    prototypeKeys: Reflect.ownKeys(C.prototype),
    members: ['getInfo', 'getIcon'].map(key => ({key, name: C.prototype[key].name,
      length: C.prototype[key].length, ...descriptor(C.prototype, key)})),
    properties: ['info', 'icon'].map(key => descriptor(first, key)),
    className: descriptor(C, 'name'), classLength: descriptor(C, 'length')};
}
assert.deepEqual(observe(actual), observe(reference));
const again = await load(`export const recovered = ${run.stdout};`, 'recovered');
assert.notEqual(actual, again); assert.notEqual(actual.prototype, again.prototype);
assert.notEqual(actual.prototype.getInfo, again.prototype.getInfo);
const treeRun = spawnSync(tool, [abc, '16948', '1', '--tree'], {encoding: 'utf8', timeout: 60_000});
assert.equal(treeRun.status, 0, treeRun.stderr);
assert.doesNotMatch(treeRun.stdout, /\$hole|\$value|extends|INCOMPLETE/);
const create = await load(`export const recovered = ${treeRun.stdout};`, 'recovered');
const composed = create(), repeated = create();
assert.deepEqual(observe(composed), observe(reference));
assert.notEqual(composed, repeated);
assert.notEqual(composed.prototype, repeated.prototype);
assert.notEqual(composed.prototype.getInfo, repeated.prototype.getInfo);
