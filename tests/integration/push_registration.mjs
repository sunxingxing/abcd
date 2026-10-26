import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc, paired] = process.argv.slice(2);
const run = spawnSync(tool, [abc, '3753', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000});
assert.equal(run.status, 0, run.stderr);
const output = JSON.parse(run.stdout.trim());
assert.equal(output.record, 3444); assert.equal(output.source_complete, true);
assert.equal(output.executable, false); assert.equal(output.legacy_writer_used, undefined);
assert.ok(output.source.includes('registerDefaultEvent()') && output.source.includes('registerIMEvent()'));
assert.equal(output.source.match(/\?\./g)?.length, 2);
const reference = stripTypeScriptTypes(readFileSync(paired, 'utf8'));
const token = {}, failure = {code: 23, message: 'failure'};
const parsedValues = [undefined, null, false, 0, '', true, 1, 1n, Symbol('value'), {}, []];
async function observe(source, method, parsed, failAt, replace) {
  const trace = []; let step = 0, callback, current, version = 0;
  const event = (...parts) => {trace.push(parts); if (++step === failAt) throw failure;};
  const hilog = new Proxy({}, {get(_target, name, receiver) {
    assert.equal(receiver, hilog); event('get-log', name, version); const saved = version;
    const fn = function(...args) {assert.equal(this, hilog); event('log', name, saved, ...args);};
    Object.defineProperty(fn, 'call', {get() {assert.fail('log call helper');}}); return fn;
  }});
  const pushService = {receiveMessage(kind, owner, fn) {
    assert.equal(this, pushService); assert.equal(owner, current); callback = fn; event('register', kind);
  }};
  const JSONStub = new Proxy({}, {get(_target, name, receiver) {
    assert.equal(receiver, JSONStub); event('get-json', name, version); const saved = version;
    const fn = function(input) {
      assert.equal(this, JSONStub); event('json', name, saved, input);
      if (replace) ++version;
      return name === 'parse' ? parsed : input === undefined ? undefined : token;
    };
    Object.defineProperty(fn, 'call', {get() {assert.fail('JSON call helper');}}); return fn;
  }});
  const context = vm.createContext({JSON: JSONStub});
  const stubs = new Map();
  const dependency = async name => {
    if (!stubs.has(name)) {
      const module = name === '@ohos:app.ability.UIAbility' ?
        new vm.SyntheticModule(['default'], function() {this.setExport('default', class UIAbility {});}, {context}) :
        name === '@ohos:hilog' ? new vm.SyntheticModule(['default'], function() {this.setExport('default', hilog);}, {context}) :
        name === '@hms:core.push.pushService' ? new vm.SyntheticModule(['default'], function() {this.setExport('default', pushService);}, {context}) :
        name.startsWith('@normalized:') ? new vm.SyntheticModule(['VoipCallService'], function() {this.setExport('VoipCallService', {});}, {context}) : null;
      assert.ok(module, name); stubs.set(name, module);
    }
    return stubs.get(name);
  };
  const module = new vm.SourceTextModule(source, {context}); await module.link(dependency); await module.evaluate();
  // Only the two registration methods and their recovered callbacks are tested.
  // Constructing the incomplete recovered derived class is outside this test.
  current = Object.create(module.namespace.default.prototype);
  Reflect.apply(current[method], current, []);
  let escaped;
  if (callback) {
    const payload = {get data() {event('data'); return 'payload';}};
    try {Reflect.apply(callback, {wrongReceiver: true}, [payload]);} catch (error) {escaped = error;}
  }
  return {trace, escaped};
}
let cases = 0;
for (const method of ['registerDefaultEvent', 'registerIMEvent'])
  for (const raw of parsedValues) for (const failAt of [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])
    for (const replace of [false, true]) {
      const parsed = raw && typeof raw === 'object' ? Object.defineProperty({}, 'notification', {
        configurable: true, get() {return token;},
      }) : raw;
      const expected = await observe(reference, method, parsed, failAt, replace);
      const actual = await observe(output.source, method, parsed, failAt, replace);
      assert.deepEqual(actual.trace, expected.trace); assert.equal(actual.escaped, expected.escaped); ++cases;
    }
console.log(`${cases} paired Push registration/callback scenarios passed; record constructor remains incomplete`);
