import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const report = JSON.parse(execFileSync(tool, [abc, '9057', '--record-source', '--value-source'],
  {encoding: 'utf8', timeout: 60_000}));
assert.equal(report.record, 8917);
assert.equal(report.source_complete, true);
assert.equal(report.legacy_writer_used, false);
assert.match(report.source, /JSON\.stringify\(err\)\)\s*\?\?\s*""/);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
const values = [undefined, null, false, 0, -0, NaN, '', 'text', 0n, 1n, Symbol('value'), {}, []];

async function observe(source, serialized, code, failAt, mutate) {
  const trace = [], failure = {code: 17, message: 'failure'};
  const main = {}, roles = new Map([[main, 'main']]);
  if (serialized !== null && typeof serialized === 'object') roles.set(serialized, 'serialized');
  let step = 0, callback, callbackError;
  const event = (label, ...args) => {
    trace.push([label, ...args.map(value => roles.get(value) ?? value)]);
    if (++step === failAt) throw failure;
  };
  const hilog = {info(...args) { assert.equal(this, hilog); event('info', ...args); },
    get error() {
      event('get-error');
      const call = function(...args) { assert.equal(this, hilog); event('error', ...args); };
      Object.defineProperty(call, 'call', {get() { assert.fail('mutable call helper'); }});
      return call;
    }};
  const json = {get stringify() {
    event('get-stringify');
    return function(error) {
      assert.equal(this, json); assert.equal(error, callbackError); event('stringify');
      if (mutate) Object.defineProperty(hilog, 'error', {get() { assert.fail('callee reread'); }});
      return serialized;
    };
  }};
  class WindowUtil {
    constructor(window) { assert.equal(window, main); roles.set(this, 'window-util'); event('construct', window); }
    setUIContext() { assert.equal(roles.get(this), 'window-util'); event('set-context'); }
    updateWindowInfo() { assert.equal(roles.get(this), 'window-util'); event('update-info'); }
  }
  const context = vm.createContext({JSON: json,
    AppStorage: {setOrCreate(key, value) { event('store', key, value); }}});
  const root = new vm.SourceTextModule(source, {context}), modules = new Map();
  await root.link(name => {
    if (!modules.has(name)) {
      const named = name === '@normalized:N&&&commons/Index&1.0.0';
      modules.set(name, new vm.SyntheticModule(named ? ['WindowUtil'] : ['default'], function() {
        this.setExport(named ? 'WindowUtil' : 'default', named ? WindowUtil :
          name === '@ohos:hilog' ? hilog : class {});
      }, {context}));
    }
    return modules.get(name);
  });
  await root.evaluate();
  // Isolate the changed complete method and its callback; this does not claim
  // acceptance of the record's constructor, every member, or HarmonyOS host.
  const instance = Object.create(root.namespace.default.prototype);
  const stage = {getMainWindowSync() { assert.equal(this, stage); event('main-window'); return main; },
    loadContent(path, fn) { assert.equal(this, stage); event('load', path); callback = fn; }};
  let returned = false, escaped;
  try {
    instance.onWindowStageCreate(stage);
    if (callback) {
      callbackError = {get code() { event('code'); return code; }};
      callback(callbackError);
    }
    returned = true;
  } catch (error) {
    if (error === failure) escaped = 'injected';
    else {
      // A caught window-construction failure may leave windowUtil absent;
      // the successful-load callback then throws the original native TypeError.
      assert.equal(error.name, 'TypeError');
      escaped = {name: error.name, message: error.message};
    }
  }
  return {trace, returned, escaped};
}
let scenarios = 0;
for (const value of values) for (const code of [0, 1]) for (const mutate of [false, true])
  for (let failAt = 0; failAt <= 14; ++failAt) {
    assert.deepEqual(await observe(report.source, value, code, failAt, mutate),
      await observe(reference, value, code, failAt, mutate));
    ++scenarios;
  }
assert.equal(scenarios, 780);
console.log(JSON.stringify({pairedCompleteMethodScenarios: scenarios}));
