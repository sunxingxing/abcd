import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '5001', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const report = JSON.parse(run.stdout.trim());
assert.equal(report.record, 4767); assert.equal(report.source_complete, false);
assert.equal(report.executable, false); assert.equal(report.legacy_writer_used, false);
assert.ok(report.source.includes('onWindowStageCreate(') && report.source.includes('INCOMPLETE'));
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
const values = [undefined, null, false, 0, -0, NaN, '', 'text', 1n, Symbol('value'), {}, []];
async function observe(source, serialized, failAt, mutate) {
  const trace = []; let step = 0, callback;
  const failure = Symbol('failure'), token = {};
  const event = (...parts) => {trace.push(parts); if (++step === failAt) throw failure;};
  const hilog = {info(...args) {event('info', ...args);}, get error() {
    event('get-error'); const call = function(...args) {assert.equal(this, hilog); event('error', ...args);};
    Object.defineProperty(call, 'call', {get() {assert.fail('call helper');}}); return call;
  }};
  const json = {get stringify() {
    event('get-stringify'); return function(error) {
      assert.equal(this, json); assert.equal(error.code, 1); event('stringify');
      if (mutate) Object.defineProperty(hilog, 'error', {get() {assert.fail('callee reread');}});
      return serialized;
    };
  }};
  const context = vm.createContext({JSON: json, AppStorage: {setOrCreate(key, value) {event('store', key, value);}}});
  const modules = new Map();
  const root = new vm.SourceTextModule(source, {context});
  await root.link(name => {
    if (!modules.has(name)) modules.set(name, new vm.SyntheticModule(['default'], function() {
      this.setExport('default', name === '@ohos:hilog' ? hilog : name === '@ohos:app.ability.UIAbility' ? class {} : {});
    }, {context}));
    return modules.get(name);
  });
  await root.evaluate();
  const C = root.namespace.EntryAbility ?? root.namespace.default;
  const instance = Object.create(C.prototype);
  const main = {getUIContext() {event('context'); return token;}};
  const stage = {loadContent(path, fn) {assert.equal(this, stage); event('load', path); callback = fn;},
    getMainWindowSync() {assert.equal(this, stage); event('main-window'); return main;}};
  let returned = false, escaped;
  try {instance.onWindowStageCreate(stage); if (callback) callback({code: 1}, undefined); returned = true;}
  catch (error) {escaped = error;}
  if (!returned) assert.equal(escaped, failure);
  // Compare only this complete method and its error callback. Constructor,
  // updateBreakpoint and the record/platform as a whole remain unaccepted.
  return {trace, returned};
}
let cases = 0;
for (const value of values) for (const mutate of [false, true]) for (let failAt = 0; failAt <= 9; ++failAt) {
  assert.deepEqual(await observe(report.source, value, failAt, mutate), await observe(reference, value, failAt, mutate)); ++cases;
}
console.log(`${cases} paired pasteboard recovered callback, nullish value, callee mutation and exception-order scenarios passed; incomplete record retained`);
