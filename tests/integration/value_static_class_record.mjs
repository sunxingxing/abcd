import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '7312', '--record-source', '--value-source'],
  { encoding: 'utf8', timeout: 60_000 });
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree');
assert.equal(report.legacy_writer_used, false);
assert.doesNotMatch(report.source, /INCOMPLETE|\$value|\.call\(|Object\.defineProperty/);
assert.match(report.source, /static TEXT_OPACITY = \[/);
async function load(source, setters = []) {
  const context = vm.createContext({ setters });
  vm.runInContext(`for (const key of setters) Object.defineProperty(Function.prototype, key,
    { configurable: true, set() { throw new Error('inherited setter'); } });`, context);
  const module = new vm.SourceTextModule(source, { context });
  await module.link(() => { throw new Error('unexpected import'); });
  assert.throws(() => module.namespace.CommonConstants, { name: 'ReferenceError' });
  await module.evaluate();
  assert.deepEqual(Object.keys(module.namespace), ['CommonConstants']);
  return module.namespace.CommonConstants;
}
const expected = await load(stripTypeScriptTypes(readFileSync(paired, 'utf8')));
const actual = await load(report.source, Object.keys(expected));
const other = await load(report.source);
assert.equal(actual.name, expected.name);
assert.equal(actual.length, expected.length);
assert.deepEqual(Reflect.ownKeys(actual), Reflect.ownKeys(expected));
assert.equal(Object.getPrototypeOf(new actual()), actual.prototype);
assert.equal(actual.prototype.constructor, actual);
for (const key of Reflect.ownKeys(expected)) {
  const a = Object.getOwnPropertyDescriptor(actual, key);
  const e = Object.getOwnPropertyDescriptor(expected, key);
  for (const flag of ['writable', 'enumerable', 'configurable']) assert.equal(a[flag], e[flag]);
  if (key === 'prototype') continue;
  assert.equal(JSON.stringify(a.value), JSON.stringify(e.value));
  if (Array.isArray(a.value)) {
    assert.notEqual(a.value, other[key]);
    a.value[0] = 'changed';
    assert.equal(other[key][0], e.value[0]);
  }
}
