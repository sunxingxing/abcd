import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
const [tool, abc, paired] = process.argv.slice(2);
const result = spawnSync(tool, [abc, '--fields'], { encoding: 'utf8' });
assert.equal(result.status, 0, result.stderr);
assert.match(result.stdout, /static FULL_PERCENT =/);
assert.match(result.stdout, /static FONT_WEIGHT_500 =/);
assert.match(result.stdout, /static TEXT_OPACITY = \[/);
assert.doesNotMatch(result.stdout, /INCOMPLETE|Object\.defineProperty|this\.[A-Z_]+\s*=/);
const reference = new vm.SourceTextModule(stripTypeScriptTypes(readFileSync(paired, 'utf8')));
await reference.link(() => { throw new Error('unexpected dependency'); });
await reference.evaluate();
const expected = reference.namespace.CommonConstants;
let setterCalls = 0;
class Base {}
for (const key of Object.keys(expected)) {
  Object.defineProperty(Base, key, { configurable: true, set() { ++setterCalls; } });
}
const create = vm.runInNewContext(`(Base) => class extends Base {\n${result.stdout}\n}`);
const a = create(Base), b = create(Base);
assert.deepEqual(Object.keys(a), Object.keys(expected));
for (const key of Object.keys(expected)) {
  const actual = Object.getOwnPropertyDescriptor(a, key);
  const oracle = Object.getOwnPropertyDescriptor(expected, key);
  assert.equal(actual.writable, oracle.writable);
  assert.equal(actual.enumerable, oracle.enumerable);
  assert.equal(actual.configurable, oracle.configurable);
  assert.equal(JSON.stringify(actual.value), JSON.stringify(oracle.value));
  if (Array.isArray(actual.value)) {
    assert.notEqual(actual.value, b[key]);
    actual.value[0] = 'changed';
    assert.equal(b[key][0], oracle.value[0]);
  }
}
assert.equal(setterCalls, 0);
