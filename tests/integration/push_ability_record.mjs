import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {stripTypeScriptTypes} from 'node:module';
import {dirname, join} from 'node:path';
import vm from 'node:vm';

const [tool, abc, pairedPath] = process.argv.slice(2);
// Finish ABC-only generation before opening the independent reference source.
const report = JSON.parse(execFileSync(tool, [abc, '3753', '--record-source',
  '--value-source', '--language-constructors'], {encoding: 'utf8', timeout: 60_000}));
assert.equal(report.record, 3444); assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree'); assert.equal(report.legacy_writer_used, false);
const serviceReport = JSON.parse(execFileSync(tool, [abc, '12208', '--record-source', '--value-source'],
  {encoding: 'utf8', timeout: 60_000}));
assert.equal(serviceReport.source_complete, true); assert.equal(serviceReport.legacy_writer_used, false);
assert.equal(serviceReport.route, 'value_tree');
const reference = stripTypeScriptTypes(readFileSync(pairedPath, 'utf8'));
const serviceReference = stripTypeScriptTypes(readFileSync(join(dirname(pairedPath), '../service/VoipCallService.ts'), 'utf8'));
assert.match(report.source, /class PushMessageAbility extends \(UIAbility\)/);
assert.match(report.source, /super\(\.\.\./);
assert.match(report.source, /receiveMessage\(/);
assert.match(report.source, /\.then\(/);
assert.doesNotMatch(report.source, /INCOMPLETE|UNRECOVERED|\.call\(|Reflect\.construct|\bswitch\s*\(/);
const kinds = ['DEFAULT', 'IM', 'BACKGROUND', 'VoIP'];
const methods = [
  ['onCreate', 2], ['onDestroy', 0], ['onWindowStageCreate', 1], ['onWindowStageDestroy', 0],
  ['onForeground', 0], ['onBackground', 0], ['registerDefaultEvent', 0], ['registerIMEvent', 0],
  ['registerBackgroundEvent', 0], ['registerVoIPEvent', 0], ['registerEvent', 0],
];
const serviceSpecifier = '@normalized:N&&&entry/src/main/ets/service/VoipCallService&';
async function observe(source, scenario) {
  const trace = [], callbacks = [], requests = [], originals = new Map();
  const token = {}, abort = {}, failure = {};
  let step = 0, version = 0, serviceModule, Type;
  const owners = new WeakMap();
  const event = (name, ...args) => { trace.push([name, ...args]); if (++step === scenario.failAt) throw failure; };
  for (const key of ['code', 'message']) Object.defineProperty(failure, key, {get() {
    event('failure-get', key); if (scenario.errorGetter === key) throw abort;
    return key === 'code' ? 23 : 'failure';
  }});
  const hilog = new Proxy({}, {get(_target, name, receiver) {
    assert.equal(receiver, hilog); event('log-get', name, version); const saved = version;
    const fn = function(...args) { assert.equal(this, hilog); event('log-call', name, saved, ...args); };
    Object.defineProperty(fn, 'call', {get() { assert.fail('log.call lookup'); }});
    return fn;
  }});
  const json = {
    parse(input) {
      assert.equal(this, json); event('parse', input); ++version;
      const raw = scenario.parsed;
      if (raw && typeof raw === 'object') return {get notification() { event('notification'); return 'notification'; }};
      return raw;
    },
    stringify(input) { assert.equal(this, json); event('stringify', input === undefined ? 'undefined' : typeof input); ++version; return input === undefined ? undefined : 'encoded'; },
  };
  function monitor(promise, request) {
    request.owned.push(promise);
    // Observe dangling chains too, so a deliberately throwing catch getter or
    // callback cannot create a test-process unhandled rejection.
    promise.then(() => {}, () => {});
    const wrapper = {};
    for (const method of ['then', 'catch']) Object.defineProperty(wrapper, method, {get() {
      event('promise-get', request.id, method);
      return function(callback) {
        assert.equal(this, wrapper); event('promise-call', request.id, method);
        return monitor(Promise.prototype[method].call(promise, callback), request);
      };
    }});
    return wrapper;
  }
  function service(label) {
    const value = {};
    Object.defineProperty(value, 'processVoIPMainMsg', {get() {
      event('service-get', label);
      return function(data, context) {
        assert.equal(this, value); assert.equal(data, 'payload');
        event('service-call', label, context.owner);
        const request = {id: requests.length, owned: []}; requests.push(request);
        const promise = new Promise((resolve, reject) => { request.resolve = resolve; request.reject = reject; });
        return monitor(promise, request);
      };
    }});
    return value;
  }
  const push = {};
  Object.defineProperty(push, 'receiveMessage', {get() {
    event('receive-get');
    return function(kind, owner, callback) {
      assert.equal(this, push); assert.ok(owners.has(owner)); assert.equal(callback.length, 1);
      callbacks.push({kind, owner, callback}); event('receive-call', kind, owners.get(owner));
    };
  }});
  class UIAbility {
    constructor(...args) {
      if (scenario.mode === 'lifecycle') {
        assert.equal(args.length, 2); assert.equal(args[0], 'parent'); assert.equal(args[1], token);
      }
      event('construct', ...args); assert.equal(new.target, Type);
      owners.set(this, originals.size); originals.set(this, originals.size);
      Object.defineProperty(this, 'context', {configurable: true, get() {
        event('context', originals.get(this)); return {owner: originals.get(this)};
      }});
    }
  }
  const context = vm.createContext({JSON: json, canIUse(name) {
    assert.equal(this, undefined); assert.equal(name, 'SystemCapability.Telephony.VoipCallManager');
    event('capability'); return scenario.capability;
  }});
  const hosts = new Map([
    ['@ohos:app.ability.UIAbility', ['default', UIAbility]],
    ['@ohos:hilog', ['default', hilog]],
    ['@hms:core.push.pushService', ['default', push]],
    [serviceSpecifier, ['VoipCallService', service('initial')]],
  ]);
  const dependencies = new Map();
  const module = new vm.SourceTextModule(source, {context});
  await module.link(name => {
    assert.ok(hosts.has(name), name);
    if (!dependencies.has(name)) {
      const [exportName, value] = hosts.get(name);
      dependencies.set(name, new vm.SyntheticModule([exportName], function() { this.setExport(exportName, value); }, {context}));
    }
    return dependencies.get(name);
  });
  assert.deepEqual(Object.getOwnPropertyNames(module.namespace), ['default']);
  assert.throws(() => module.namespace.default, {name: 'ReferenceError'});
  await module.evaluate();
  serviceModule = dependencies.get(serviceSpecifier); Type = module.namespace.default;
  assert.equal(Type.name, 'PushMessageAbility'); assert.equal(Type.length, 0);
  assert.equal(Object.getPrototypeOf(Type), UIAbility);
  assert.equal(Object.getPrototypeOf(Type.prototype), UIAbility.prototype);
  assert.throws(() => Type(), {name: 'TypeError'});
  for (const [name, length] of methods) {
    const descriptor = Object.getOwnPropertyDescriptor(Type.prototype, name);
    assert.equal(descriptor.value.name, name); assert.equal(descriptor.value.length, length);
    assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable], [true, false, true]);
  }
  let escaped = 'none';
  const invoke = fn => {
    try { return fn(); } catch (error) {
      assert.ok(error === failure || error === abort, 'unexpected record runtime failure');
      escaped = error === abort ? 'getter-failure' : 'failure'; return undefined;
    }
  };
  if (scenario.mode === 'lifecycle') invoke(() => {
    const instance = new Type('parent', token);
    assert.equal(Object.getPrototypeOf(instance), Type.prototype);
    assert.equal(instance.onCreate({}, {}), undefined);
    const window = {};
    let windowCallback;
    Object.defineProperty(window, 'loadContent', {get() {
      event('window-get');
      return function(path, callback) {
        assert.equal(this, window); assert.equal(path, 'pages/Index'); event('window-call'); windowCallback = callback;
      };
    }});
    assert.equal(instance.onWindowStageCreate(window), undefined);
    assert.equal(Reflect.apply(windowCallback, {wrongReceiver: true}, [{get code() { event('window-code'); return scenario.code; }}]), undefined);
    for (const name of ['onWindowStageDestroy', 'onForeground', 'onBackground', 'onDestroy'])
      assert.equal(instance[name](), undefined);
  });
  else {
    for (let owner = 0; owner < 2; ++owner) invoke(() => {
      const instance = new Type(owner);
      if (owner) serviceModule.setExport('VoipCallService', service('replacement'));
      assert.equal(instance.registerEvent(), undefined);
    });
    // Each closure must retain its original instance after another registration.
    serviceModule.setExport('VoipCallService', service('delivery'));
    for (const {kind, owner, callback} of callbacks) invoke(() => {
      const payload = {get data() {
        event('payload', kind, originals.get(owner));
        if (scenario.replaceArgument) serviceModule.setExport('VoipCallService', service('during-argument'));
        return 'payload';
      }};
      assert.equal(Reflect.apply(callback, {wrongReceiver: true}, [payload]), undefined);
    });
  }
  // Settlement order is controlled explicitly; record callbacks are actual
  // native Promise reactions. Await every owned chain, including rejection.
  for (const request of scenario.reverse ? [...requests].reverse() : requests) {
    trace.push(['release', request.id]);
    if (request.id === scenario.rejectRequest) request.reject(failure); else request.resolve(token);
    const settled = await Promise.allSettled(request.owned);
    trace.push(['settled', request.id, settled.map(result => {
      if (result.status === 'rejected') {
        assert.ok(result.reason === failure || result.reason === abort);
        return result.reason === abort ? 'getter-failure' : 'failure';
      }
      // A tracked then/catch wrapper intentionally records continuation results,
      // while the base service promise resolves the original token.
      assert.ok(result.value === token || result.value === undefined); return result.value === token ? 'token' : 'undefined';
    })]);
  }
  if (!scenario.failAt) {
    const expectedOwners = scenario.mode === 'lifecycle' ? [0] : [0, 1];
    assert.deepEqual(callbacks.map(({kind, owner}) => [kind, originals.get(owner)]),
      expectedOwners.flatMap(owner => kinds.map(kind => [kind, owner])));
  }
  return {trace, escaped, registrations: callbacks.map(({kind, owner}) => [kind, originals.get(owner)])};
}
// Both actual recovered records form an ESM graph. Only platform boundaries
// are doubles here; no service body is replaced with a fixture function.
async function connected(abilitySource, serviceSource, reverse, reject) {
  const trace = [], callbacks = [], listeners = [], pending = [], buffers = new WeakMap();
  const failure = {code: 23, message: 'host failure'};
  let completed = 0, owner = 0;
  const hilog = {};
  for (const level of ['info', 'error']) hilog[level] = function(...args) {
    assert.equal(this, hilog); trace.push([level, ...args]);
    if (args.at(-1) === 'succeed to process VoIP message' || args[2] === 'Failed to process VoIP message: %{public}d %{public}s') ++completed;
  };
  class UIAbility {
    constructor() {
      const id = owner++;
      this.context = {resourceManager: {getRawFileContent(name) {
        assert.equal(this, instanceContext.resourceManager); assert.equal(name, 'example.png');
        trace.push(['raw-request', id]);
        return new Promise((resolve, reject) => pending.push({id, resolve, reject}));
      }}};
      const instanceContext = this.context;
      this.id = id;
    }
  }
  const push = {receiveMessage(kind, receiver, callback) {
    assert.equal(this, push); trace.push(['register', kind, receiver.id]); callbacks.push({kind, receiver, callback});
  }};
  const voip = {
    VoipCallType: {VOIP_CALL_VOICE: 0, VOIP_CALL_VIDEO: 1},
    VoipCallState: {VOIP_CALL_STATE_RINGING: 10, VOIP_CALL_STATE_ANSWERED: 11,
      VOIP_CALL_STATE_ACTIVE: 12, VOIP_CALL_STATE_DISCONNECTED: 13},
    VoipCallUiEvent: {VOIP_CALL_EVENT_VOICE_ANSWER: 1, VOIP_CALL_EVENT_VIDEO_ANSWER: 2,
      VOIP_CALL_EVENT_REJECT: 3, VOIP_CALL_EVENT_HANGUP: 4},
    on(name, callback) { assert.equal(this, voip); trace.push(['on', name]); listeners.push(callback); },
    off(name) { assert.equal(this, voip); trace.push(['off', name]); },
    reportIncomingCall(call) {
      assert.equal(this, voip);
      trace.push(['incoming', call.callId, call.voipCallType, call.userProfile.id]);
      return reject === 'incoming' ? Promise.reject(failure) : Promise.resolve(0);
    },
    reportCallStateChange(id, state) { assert.equal(this, voip); trace.push(['state', id, state]); return Promise.resolve(); },
  };
  const image = {createImageSource(buffer) {
    assert.equal(this, image); const id = buffers.get(buffer) ?? -1;
    trace.push(['image', id, buffer.byteLength]);
    const source = {createPixelMap() {
      assert.equal(this, source); trace.push(['pixel', id]);
      const pixel = {id, getImageInfo(callback) {
        assert.equal(this, pixel); trace.push(['info', id]); callback(null, {size: {width: 2, height: 3}});
      }};
      return reject === 'pixel' ? Promise.reject(failure) : Promise.resolve(pixel);
    }};
    return source;
  }};
  const context = vm.createContext({Uint8Array, canIUse() { assert.equal(this, undefined); return true; }});
  const module = new vm.SourceTextModule(abilitySource, {context});
  const service = new vm.SourceTextModule(serviceSource, {context});
  const hosts = new Map([
    ['@ohos:app.ability.UIAbility', UIAbility], ['@ohos:hilog', hilog],
    ['@hms:core.push.pushService', push], ['@hms:telephony.voipCall', voip], ['@ohos:multimedia.image', image],
  ]);
  const dependencies = new Map();
  await module.link(name => {
    if (name === serviceSpecifier) return service;
    assert.ok(hosts.has(name), name);
    if (!dependencies.has(name)) dependencies.set(name, new vm.SyntheticModule(['default'], function() {
      this.setExport('default', hosts.get(name));
    }, {context}));
    return dependencies.get(name);
  });
  await module.evaluate();
  for (let id = 0; id < 2; ++id) new module.namespace.default().registerVoIPEvent();
  for (const {receiver, callback} of callbacks) {
    const data = JSON.stringify({callId: `call:${receiver.id}`, data: JSON.stringify({scene: receiver.id ? 'video' : 'voice'})});
    assert.equal(Reflect.apply(callback, {wrongReceiver: true}, [{data}]), undefined);
  }
  assert.equal(pending.length, 2); assert.equal(completed, 0);
  const maxResumeMicrotasks = 32; // Fixture deadlock bound, not a language scheduling premise.
  const advance = async predicate => {
    for (let n = 0; n < maxResumeMicrotasks && !predicate(); ++n) await Promise.resolve();
    assert.ok(predicate(), 'released resource request must reach its completion callback');
  };
  for (const request of reverse ? [...pending].reverse() : pending) {
    const prior = completed; trace.push(['release', request.id]);
    if (reject === 'raw') request.reject(failure);
    else {
      const data = new Uint8Array([request.id, 7]); buffers.set(data.buffer, request.id); request.resolve(data);
    }
    await advance(() => completed > prior);
  }
  assert.equal(completed, 2);
  for (const callback of listeners) await callback({voipCallUiEvent: 1});
  const Service = service.namespace.VoipCallService, original = Service.processVoipCallEvent;
  let closing;
  Service.processVoipCallEvent = function(...args) {
    assert.equal(this, Service); closing = Reflect.apply(original, this, args); return closing;
  };
  assert.equal(Service.close(), undefined); assert.equal(typeof closing.then, 'function');
  await closing;
  return {trace, callId: service.namespace.VoipCallService.callId};
}
let connectedCases = 0;
for (const reverse of [false, true]) for (const reject of ['none', 'raw', 'pixel', 'incoming']) {
  assert.deepEqual(await connected(report.source, serviceReport.source, reverse, reject),
    await connected(reference, serviceReference, reverse, reject));
  ++connectedCases;
}
let lifecycleCases = 0, deliveryCases = 0;
for (const code of [undefined, null, false, 0, NaN, 1, 'error']) for (let failAt = 0; failAt <= 45; ++failAt) {
  const scenario = {mode: 'lifecycle', code, failAt, parsed: null, capability: false};
  assert.deepEqual(await observe(report.source, scenario), await observe(reference, scenario)); ++lifecycleCases;
}
const values = [undefined, null, false, 0, '', true, 1, 1n, Symbol('value'), {}, []];
for (const parsed of values) for (const capability of [false, true]) for (const reverse of [false, true])
  for (const rejectRequest of [-1, 0, 1]) for (const replaceArgument of [false, true])
    for (const failAt of [0, 4, 12, 24, 40, 64, 80]) {
      const scenario = {mode: 'delivery', parsed, capability, reverse, rejectRequest, replaceArgument, failAt};
      assert.deepEqual(await observe(report.source, scenario), await observe(reference, scenario)); ++deliveryCases;
    }
for (const errorGetter of ['code', 'message']) for (const failAt of [4, 12, 24, 40, 64, 80]) {
  const scenario = {mode: 'delivery', parsed: {}, capability: true, rejectRequest: 0, errorGetter, failAt};
  assert.deepEqual(await observe(report.source, scenario), await observe(reference, scenario)); ++deliveryCases;
}
const mutations = [
  {name: 'background registration removed', change: text => text.replace('this.registerBackgroundEvent();', 'void 0;'),
   scenario: {mode: 'delivery', parsed: {}, capability: false, failAt: 0}},
  {name: 'captured context lookup omitted', change: text => text.replace('$local2.context', '({owner: -1})'),
   scenario: {mode: 'delivery', parsed: {}, capability: true, failAt: 0, rejectRequest: -1}},
  {name: 'window callback condition reversed', change: text => text.replace('if (!err.code)', 'if (err.code)'),
   scenario: {mode: 'lifecycle', code: 0, failAt: 0, capability: false}},
];
for (const {name, change, scenario} of mutations) {
  const changed = change(report.source); assert.notEqual(changed, report.source);
  new vm.SourceTextModule(changed); // Invalid syntax cannot count as detection.
  let detected = false;
  try { assert.deepEqual(await observe(changed, scenario), await observe(reference, scenario)); }
  catch (error) { if (!(error instanceof assert.AssertionError)) throw error; detected = true; }
  assert.equal(detected, true, name);
}
console.log(JSON.stringify({wholeRecord: 3444, lifecycleCases, deliveryCases, mutations: mutations.length,
  connectedCases, constructorMode: 'language', platformService: false}));
