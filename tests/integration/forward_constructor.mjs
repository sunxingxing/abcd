import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '9460', '--record-source', '--value-source'], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout);
assert.equal(report.source_complete, true);
assert.equal(report.legacy_writer_used, false);
assert.ok(report.source.includes('class extends') && !report.source.includes('constructor('));
assert.ok(!report.source.includes('super(') && !report.source.includes('INCOMPLETE'));
const original = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
const payloads = [undefined, null, false, 0, -0, NaN, 1n, Symbol('arg'), {}, []];
let cases = 0;
async function observe(source, mode, args, alternate, behavior) {
  const trace = [], token = {}, failure = Symbol('failure');
  let C, requested;
  function Base(...received) {
    assert.equal(new.target, requested);
    assert.equal(received.length, args.length + (mode === 'bound' ? 1 : 0));
    const values = mode === 'bound' ? received.slice(1) : received;
    for (let i = 0; i < args.length; ++i) assert.ok(Object.is(values[i], args[i]));
    trace.push(['base', received.length, new.target === C ? 'class' : 'alternate']);
    if (behavior === 'throw') throw failure;
    if (behavior === 'object') return token;
    return 5;
  }
  let parent = mode === 'bound' ? Base.bind(null, 'prefix') : Base;
  if (mode === 'proxy') parent = new Proxy(Base, {construct(target, list, newTarget) {
    trace.push(['trap', list.length]);
    return Reflect.construct(target, list, newTarget);
  }});
  const context = vm.createContext({JSON, record: (...parts) => trace.push(parts)});
  const hilog = {info(...parts) {trace.push(['info', ...parts]);}};
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => new vm.SyntheticModule(['default'], function() {
    this.setExport('default', name === '@ohos:hilog' ? hilog : Base);
  }, {context}));
  await root.evaluate();
  C = root.namespace.default;
  assert.equal(C.length, 0);
  Object.setPrototypeOf(C, parent); // Parent identity is selected at invocation.
  function Target() {}
  requested = alternate ? Target : C;
  vm.runInContext("Object.defineProperty(Array.prototype, Symbol.iterator, {get() {throw new Error('unexpected iterator access');}})", context);
  let value, threw = false;
  try {value = Reflect.construct(C, args, requested);}
  catch (error) {assert.equal(error, failure); threw = true;}
  if (!threw) {
    assert.equal(value === token, behavior === 'object');
    assert.equal(Object.getPrototypeOf(value), behavior === 'object' ? Object.prototype : requested.prototype);
    if (behavior !== 'object') assert.deepEqual(Reflect.ownKeys(value), []);
  }
  return {trace, threw};
}
for (const payload of payloads) for (const mode of ['direct', 'bound', 'proxy'])
  for (const alternate of [false, true]) for (const behavior of ['primitive', 'object', 'throw']) {
    const args = [payload, {}, payload];
    assert.deepEqual(await observe(report.source, mode, args, alternate, behavior),
      await observe(original, mode, args, alternate, behavior)); ++cases;
  }
for (const size of [0, 1, 2, 200, 201]) {
  const args = Array.from({length: size}, (_, i) => ({i}));
  assert.deepEqual(await observe(report.source, 'proxy', args, false, 'primitive'),
    await observe(original, 'proxy', args, false, 'primitive')); ++cases;
}
async function methods(source, value, failAt) {
  const trace = []; let step = 0;
  const failure = Symbol('failure');
  const event = (...parts) => {trace.push(parts); if (++step === failAt) throw failure;};
  const hilog = {get info() {event('get-info'); return function(...parts) {
    assert.equal(this, hilog); event('info', ...parts);
  };}};
  const json = {get stringify() {event('get-stringify'); return function(input) {
    assert.equal(this, json); assert.equal(input, value); event('stringify'); return 'serialized';
  };}};
  const context = vm.createContext({JSON: json});
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => new vm.SyntheticModule(['default'], function() {
    this.setExport('default', name === '@ohos:hilog' ? hilog : class {});
  }, {context}));
  await root.evaluate();
  const instance = new root.namespace.default();
  const outcomes = [];
  for (const [method, args] of [['onBackup', []], ['onRestore', [value]]]) {
    let returned;
    try {const promise = instance[method](...args); assert.equal(typeof promise.then, 'function'); returned = await promise; outcomes.push('resolve');}
    catch (error) {assert.equal(error, failure); outcomes.push('reject'); continue;}
    assert.equal(returned, undefined);
  }
  return {trace, outcomes};
}
let methodCases = 0;
for (const value of payloads) for (let failAt = 0; failAt <= 6; ++failAt) {
  assert.deepEqual(await methods(report.source, value, failAt), await methods(original, value, failAt)); ++methodCases;
}
console.log(cases + ' paired raw-frame constructor scenarios and ' + methodCases + ' paired async method scenarios passed; no platform/live acceptance');
