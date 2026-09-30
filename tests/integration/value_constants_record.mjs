import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a');
// Paired source stays exclusively in this oracle. The engine receives ABC only.
const run = spawnSync(tool, [abc, '8889', '--record-source', '--value-source'],
  {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr + run.stdout);
const report = JSON.parse(run.stdout);
assert.equal(report.source_complete, true);
assert.equal(report.legacy_writer_used, false);
assert.equal(report.route, 'value_tree');
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
async function load(source) {
  const context = vm.createContext({});
  vm.runInContext('Object.defineProperty(Array.prototype, "0", {set() { throw new Error("inherited setter"); }, configurable: true});', context);
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => { throw new Error('unexpected import'); });
  assert.throws(() => module.namespace.ROUTES, {name: 'ReferenceError'});
  await module.evaluate();
  assert.deepEqual(Object.keys(module.namespace), ['ROUTES']);
  return module.namespace.ROUTES;
}
const actual = await load(report.source), expected = await load(reference);
const forward = new Map(), backward = new Map();
function compare(a, b) {
  if (b === null || typeof b !== 'object') { assert.ok(Object.is(a, b)); return; }
  assert.ok(a !== null && typeof a === 'object');
  if (forward.has(b)) { assert.equal(a, forward.get(b)); return; }
  assert.equal(backward.has(a), false, 'distinct source identities merged');
  forward.set(b, a); backward.set(a, b);
  assert.equal(Array.isArray(a), Array.isArray(b));
  assert.deepEqual(Reflect.ownKeys(a), Reflect.ownKeys(b));
  for (const key of Reflect.ownKeys(b)) {
    const x = Object.getOwnPropertyDescriptor(a, key), y = Object.getOwnPropertyDescriptor(b, key);
    for (const attribute of ['writable', 'enumerable', 'configurable', 'get', 'set'])
      assert.equal(x[attribute], y[attribute]);
    compare(x.value, y.value);
  }
}
compare(actual, expected);
// Buffer placeholders must disappear independently of behavioral agreement.
// This does not yet accept the whole record's source representation quality.
assert.match(report.source, /export \{ ROUTES/);
assert.ok(!report.source.includes('$acc'));
assert.match(report.source, /title: /);
assert.ok(!report.source.includes('["title"]:'), 'ordinary object key should use native syntax');
const retainedPlaceholders = /(?:\["(?:title|name|children|params)"\]|\b(?:title|name|children|params)): null/.test(report.source);
assert.equal(retainedPlaceholders, false, 'overwritten buffer placeholders remain');
console.log(JSON.stringify({semantic: 'passed', retainedBufferPlaceholders: retainedPlaceholders}));
