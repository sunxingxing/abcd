import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '4386', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout.trim());
assert.equal(report.record, 4224);
assert.equal(report.source_complete, false);
assert.equal(report.executable, false);
assert.equal(report.legacy_writer_used, false);
assert.ok(report.source.includes('onCreate(') && report.source.includes('onNewWant('));
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
const values = [undefined, null, false, 0, -0, NaN, '', 'text', 1n, Symbol('value'), {}, []];

async function load(source) {
  const context = vm.createContext({});
  const state = {event: undefined};
  let lookups = 0;
  const hilog = {get info() {
    const ordinal = ++lookups;
    state.event('get-info', ordinal);
    return function(...args) {assert.equal(this, hilog); state.event('info', ordinal, ...args);};
  }};
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => new vm.SyntheticModule(['default'], function() {
    this.setExport('default', name === '@ohos:hilog' ? hilog : class {});
  }, {context}));
  await root.evaluate();
  // Only complete methods are exercised. The incomplete constructor must never
  // be called, and these comparisons confer no record/platform acceptance.
  const C = root.namespace.default ?? root.namespace.TestAbility;
  assert.equal(typeof C, 'function');
  return {C, state, reset() {lookups = 0;}};
}

const recovered = await load(report.source), original = await load(reference);
function observe(subject, method, absent, payload, mutate, failAt) {
  subject.reset();
  const trace = [], failure = Symbol('failure'); let step = 0;
  subject.state.event = (...parts) => {trace.push(parts); if (++step === failAt) throw failure;};
  const event = subject.state.event;
  const parameters = {get testKey() {
    event('get-key');
    if (mutate) Object.defineProperty(want, 'parameters', {get() {assert.fail('parameters reread');}});
    return payload;
  }};
  const want = {get parameters() {event('get-parameters'); return absent === 0 ? parameters : absent === 1 ? null : undefined;}};
  const storage = {get setOrCreate() {
    event('get-store');
    return function(key, value) {assert.equal(this, storage); assert.equal(value, want); event('store', key, value);};
  }};
  const instance = Object.create(subject.C.prototype);
  Object.defineProperty(instance, 'localStorage', {get() {event('get-local-storage'); return storage;}});
  let returned = false, result;
  try {result = instance[method](want, {}); returned = true;}
  catch (error) {assert.equal(error, failure);}
  // The want identity is checked above; normalize only its trace representation
  // so each isolated execution can construct its own object and mutation state.
  return {trace: trace.map(parts => parts.map(part => part === want ? 'want-identity' : part)), returned, result};
}
let cases = 0;
for (const method of ['onCreate', 'onNewWant']) for (const absent of [0, 1, 2])
  for (const value of values) for (const mutate of [false, true]) for (let failAt = 0; failAt <= 10; ++failAt) {
    assert.deepEqual(observe(recovered, method, absent, value, mutate, failAt),
      observe(original, method, absent, value, mutate, failAt));
    ++cases;
  }
console.log(`${cases} paired retained-selection method scenarios passed; incomplete constructor and record retained`);
