import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired, variant] = process.argv.slice(2);
assert.ok(variant === undefined || variant === '--entry');
const entry = variant === '--entry';
const run = spawnSync(tool, [abc, entry ? '5652' : '4386', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout.trim());
assert.equal(report.record, entry ? 5374 : 4224);
assert.equal(report.source_complete, false);
assert.equal(report.executable, false);
assert.equal(report.legacy_writer_used, undefined);
assert.ok(report.source.includes('onCreate(') && report.source.includes('onNewWant('));
assert.match(report.source, /\?\? "default value"/);
assert.equal(report.source.match(/\?\./g)?.length, 2);
assert.ok(!report.source.includes('!== null ?'), 'optional parameter read remains expanded');
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
  }, get error() {
    state.event('get-error');
    return function(...args) {assert.equal(this, hilog); state.event('error', ...args);};
  }};
  const chain = {get catch() {
    state.event('get-catch');
    return function(callback) {assert.equal(this, chain); assert.equal(typeof callback, 'function'); state.event('register-catch');};
  }};
  const pending = {get then() {
    state.event('get-then');
    return function(callback) {assert.equal(this, pending); assert.equal(typeof callback, 'function'); state.event('register-then'); return chain;};
  }};
  const push = {get getToken() {
    state.event('get-token-method');
    return function() {assert.equal(this, push); state.event('get-token'); return pending;};
  }};
  const configuration = {ColorMode: {COLOR_MODE_NOT_SET: -1}};
  context.JSON = {stringify(error) {assert.equal(error, state.failure); state.event('stringify'); return 'failure-json';}};
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => new vm.SyntheticModule(['default'], function() {
    this.setExport('default', name === '@ohos:hilog' ? hilog : name === '@hms:core.push.pushService' ? push :
      name === '@ohos:app.ability.ConfigurationConstant' ? configuration : class {});
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
  subject.state.failure = failure;
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
  const appContext = {get setColorMode() {
    event('get-set-color');
    return function(mode) {assert.equal(this, appContext); event('set-color', mode);};
  }};
  const abilityContext = {get getApplicationContext() {
    event('get-application-context');
    return function() {assert.equal(this, abilityContext); event('application-context'); return appContext;};
  }};
  Object.defineProperty(instance, 'context', {get() {event('get-context'); return abilityContext;}});
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
  for (const value of values) for (const mutate of [false, true]) for (let failAt = 0; failAt <= (entry ? 22 : 10); ++failAt) {
    assert.deepEqual(observe(recovered, method, absent, value, mutate, failAt),
      observe(original, method, absent, value, mutate, failAt));
    ++cases;
  }
console.log(`${cases} paired retained-selection method scenarios passed; incomplete constructor and record retained`);
