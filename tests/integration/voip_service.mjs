import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Independent paired-source tests only. Production receives the ABC alone.
// Host stubs exercise ordering/identity, not a HarmonyOS telephony service.
const [tool, root] = process.argv.slice(2);
const recovered = JSON.parse(execFileSync(tool, [join(root, 'modules.abc'), '12208',
  '--record-source', '--value-source'], {encoding: 'utf8'}));
assert.equal(recovered.source_complete, true);
assert.equal(recovered.legacy_writer_used, false);
// Source representation is checked independently from execution. The field
// still exists as an own undefined data property; an initializer is redundant.
assert.match(recovered.source, /static callId;/);
// A whole native object owns the optional getter and conditional enum read.
// Keep the positive predicate and field declaration order; staged assignments
// or a detached phi do not meet this representation assertion.
assert.match(recovered.source, /\(\{callId: [^,]+, voipCallType: \(\([^;]*\?\.scene\) === "video" \? voipCall\.VoipCallType\.VOIP_CALL_VIDEO : voipCall\.VoipCallType\.VOIP_CALL_VOICE\)/);
assert.match(recovered.source, /userName: "push", userProfile: [^,]+, abilityName: "VoIPUIAbility", voipCallState: voipCall\.VoipCallState\.VOIP_CALL_STATE_RINGING/);
assert.doesNotMatch(recovered.source, /\.voipCallType =|\.voipCallState =/);
const paired = stripTypeScriptTypes(readFileSync(join(root,
  'src/entry/src/main/ets/service/VoipCallService.ts'), 'utf8'));
async function loadService(source, hosts, context) {
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(hosts.has(specifier));
    return new vm.SyntheticModule(['default'], function() {
      this.setExport('default', hosts.get(specifier));
    }, {context});
  });
  await module.evaluate();
  return module.namespace.VoipCallService;
}
async function observe(source, scenario) {
  const events = [], failure = {code: 17, message: 'failure'};
  let steps = 0, listener;
  const event = (name, ...args) => {
    events.push([name, ...args]);
    if (++steps === scenario.failAt) throw failure;
  };
  const later = (name, value) => Promise.resolve().then(() => {
    event(`settle:${name}`);
    if (scenario.reject === name) throw failure;
    return value;
  });
  const enumObject = (name, values) => new Proxy(values, {get(target, key) {
    event(`${name}.${String(key)}`); return target[key];
  }});
  const ui = enumObject('ui', {VOIP_CALL_EVENT_VOICE_ANSWER: 1, VOIP_CALL_EVENT_VIDEO_ANSWER: 2,
    VOIP_CALL_EVENT_REJECT: 3, VOIP_CALL_EVENT_HANGUP: 4});
  const state = enumObject('state', {VOIP_CALL_STATE_RINGING: 10, VOIP_CALL_STATE_ANSWERED: 11,
    VOIP_CALL_STATE_ACTIVE: 12, VOIP_CALL_STATE_DISCONNECTED: 13});
  const pixel = {getImageInfo(callback) {
    assert.equal(this, pixel); event('getImageInfo');
    callback(null, scenario.noInfo ? null : {size: {width: 13, height: 7}});
  }};
  const raw = new Uint8Array([1, 2, 3]);
  const imageSource = {createPixelMap() {
    assert.equal(this, imageSource); event('createPixelMap');
    return later('pixel', scenario.noPixel ? null : pixel);
  }};
  const image = {createImageSource(buffer) {
    assert.equal(this, image); event('createImageSource', buffer.byteLength);
    assert.ok(buffer === raw.buffer || buffer.byteLength === 0);
    return imageSource;
  }};
  const resource = {getRawFileContent(name) {
    assert.equal(this, resource); event('getRawFileContent', name); return later('raw', raw);
  }};
  const contextValue = {get resourceManager() { event('resourceManager'); return resource; }};
  const voip = {
    VoipCallUiEvent: ui, VoipCallState: state,
    VoipCallType: enumObject('type', {VOIP_CALL_VOICE: 20, VOIP_CALL_VIDEO: 21}),
    on(name, callback) { assert.equal(this, voip); event('on', name); listener = callback; },
    off(name) { assert.equal(this, voip); event('off', name); },
    reportIncomingCall(call) {
      assert.equal(this, voip);
      assert.deepEqual(Array.from(Reflect.ownKeys(call)),
        ['callId', 'voipCallType', 'userName', 'userProfile', 'abilityName', 'voipCallState']);
      for (const key of Reflect.ownKeys(call)) {
        const d = Object.getOwnPropertyDescriptor(call, key);
        assert.ok(d.writable && d.enumerable && d.configurable && 'value' in d);
      }
      assert.equal(call.userProfile, scenario.noPixel ? null : pixel);
      event('incoming', call.callId, call.voipCallType, call.userName, call.abilityName, call.voipCallState);
      return later('incoming', 0);
    },
    reportCallStateChange(id, value) {
      assert.equal(this, voip); event('change', id, value); return later('change', undefined);
    },
  };
  const hilog = {};
  for (const name of ['info', 'error']) hilog[name] = function(...args) {
    assert.equal(this, hilog); event(name, ...args);
  };
  let parses = 0;
  const json = {stringify: JSON.stringify, parse(text) {
    assert.equal(this, json);
    const value = JSON.parse(text);
    if (++parses !== 2 || !scenario.sceneGetter) return value;
    const parsed = new Proxy({}, {get(target, key, receiver) {
      assert.equal(receiver, parsed); assert.equal(key, 'scene'); event('scene:get');
      if (scenario.throwScene) throw failure;
      return scenario.sceneValue;
    }});
    return parsed;
  }};
  const context = vm.createContext({Uint8Array, JSON: json});
  // Native fields use DefineOwnProperty even when they have no initializer.
  // An assignment to an inherited setter would lose the class's own field.
  vm.runInContext(`Object.defineProperty(Function.prototype, 'callId', {
    configurable: true, get() { throw new Error('inherited field read'); },
    set() { throw new Error('inherited field write'); }
  })`, context);
  const hosts = new Map([['@hms:telephony.voipCall', voip], ['@ohos:hilog', hilog], ['@ohos:multimedia.image', image]]);
  const service = await loadService(source, hosts, context);
  assert.equal(service.callId, undefined);
  const field = Object.getOwnPropertyDescriptor(service, 'callId');
  assert.deepEqual({...field}, {value: undefined, writable: true, enumerable: true, configurable: true});
  assert.equal(service.name, 'VoipCallService'); assert.equal(service.length, 0);
  for (const [name, length] of [['processVoIPMainMsg', 2], ['processVoipCallEvent', 1], ['close', 0]]) {
    const method = Object.getOwnPropertyDescriptor(service, name);
    assert.equal(method.value.name, name); assert.equal(method.value.length, length);
    assert.equal(method.enumerable, false); assert.equal(method.writable, true);
    assert.equal(method.configurable, true);
  }
  let outcome = 'return';
  try {
    if (scenario.mode === 'event') {
      service.callId = 'call';
      await service.processVoipCallEvent(scenario.input);
    } else {
      const data = JSON.stringify({data: JSON.stringify(scenario.scene), callId: scenario.callId ?? 'call'});
      await service.processVoIPMainMsg(data, contextValue);
      if (scenario.mode === 'callback' && listener) await listener({voipCallUiEvent: scenario.input});
      if (scenario.mode === 'close') {
        service.close();
        await Promise.resolve(); await Promise.resolve();
      }
    }
  } catch (error) {
    assert.equal(error, failure); outcome = 'failure';
  }
  return {events, outcome, callId: service.callId};
}

// Keep host promises unresolved until the driver releases a specific request.
// This observes suspension, concurrent requests, live field reads after await,
// and callee snapshots before argument getters, independently of ABC layouts.
async function observeScheduled(source, scenario) {
  const events = [], pending = [], outcomes = [], runs = [];
  let service, currentId = 'initial-call', finished = 0, replaced = false;
  const abort = {code: 99, message: 'original getter exception'};
  const failure = {};
  for (const key of ['code', 'message']) Object.defineProperty(failure, key, {get() {
    events.push(['error:get', key]); currentId = `error:${key}`;
    if (scenario.errorGetter === key) throw abort;
    return key === 'code' ? 17 : 'failure';
  }});
  const request = label => function(id, value) {
    assert.equal(this, voip);
    const number = pending.length;
    events.push(['request', number, label, id, value]);
    return new Promise((resolve, reject) => pending.push({number, resolve, reject, settled: false}));
  };
  const ui = new Proxy({VOIP_CALL_EVENT_VOICE_ANSWER: 1, VOIP_CALL_EVENT_VIDEO_ANSWER: 2,
    VOIP_CALL_EVENT_REJECT: 3, VOIP_CALL_EVENT_HANGUP: 4}, {get(target, key) {
    events.push(['ui:get', key]); return target[key];
  }});
  const state = new Proxy({VOIP_CALL_STATE_ANSWERED: 11, VOIP_CALL_STATE_ACTIVE: 12,
    VOIP_CALL_STATE_DISCONNECTED: 13}, {get(target, key) {
    events.push(['state:get', key]);
    if (scenario.replaceCallee && !replaced) {
      replaced = true; voip.reportCallStateChange = request('replacement');
      events.push(['replace-callee']);
    }
    return target[key];
  }});
  const voip = {VoipCallUiEvent: ui, VoipCallState: state, reportCallStateChange: request('initial')};
  const hilog = {error(...args) { assert.equal(this, hilog); events.push(['error', ...args]); }};
  service = await loadService(source, new Map([['@hms:telephony.voipCall', voip],
    ['@ohos:hilog', hilog], ['@ohos:multimedia.image', {}]]), vm.createContext({Uint8Array}));
  Object.defineProperty(service, 'callId', {configurable: true, get() {
    events.push(['callId:get', currentId]); return currentId;
  }});
  const start = input => {
    const number = runs.length;
    const run = service.processVoipCallEvent(input);
    assert.equal(typeof run.then, 'function');
    runs.push(run.then(value => {
      assert.equal(value, undefined); outcomes[number] = 'return'; ++finished;
    }, error => {
      assert.equal(error, abort); outcomes[number] = 'original-getter-exception'; ++finished;
    }));
  };
  start(scenario.input);
  const firstRequests = [1, 2, 3, 4].includes(scenario.input) ? 1 : 0;
  assert.equal(pending.length, firstRequests, 'only the first request may start before suspension');
  if (scenario.concurrent) {
    currentId = 'concurrent-call'; start(1);
    assert.equal(pending.length, firstRequests + 1);
  }
  // Native await and the foreign-realm Promise adoption can require multiple
  // microtasks. The limit detects a stuck fixture; no wall-clock sleep or
  // timeout is used to infer completion or to start dependent work.
  const advance = async predicate => {
    for (let i = 0; i < 16 && !predicate(); ++i) await Promise.resolve();
    assert.ok(predicate(), 'settled request must resume or finish its operation');
  };
  await advance(() => finished + pending.length >= runs.length);
  while (pending.some(item => !item.settled)) {
    assert.ok(pending.length <= runs.length * 2);
    const available = pending.filter(item => !item.settled);
    const item = scenario.reverse ? available.at(-1) : available[0];
    currentId = `resume:${item.number}`;
    events.push(['release', item.number, currentId]);
    item.settled = true;
    const priorRequests = pending.length, priorFinished = finished;
    if (item.number === scenario.failRequest) item.reject(failure); else item.resolve(undefined);
    await advance(() => pending.length > priorRequests || finished > priorFinished);
  }
  await advance(() => finished === runs.length);
  await Promise.all(runs);
  return {events, outcomes, currentId};
}
let cases = 0;
const compare = async scenario => {
  assert.deepEqual(await observe(recovered.source, scenario), await observe(paired, scenario),
      JSON.stringify(scenario, (key, value) => typeof value === 'bigint' ? String(value) : value));
  ++cases;
};
// The paired source and recovered source receive independent getter-backed
// parse results. This tests the actual record's nested condition, not a source
// answer supplied to the engine, and its original unhandled getter exception.
for (const sceneValue of [undefined, null, 'video', 'voice', false, 0, -0, NaN, 1n,
    Symbol('scene'), {[Symbol.toPrimitive]() { assert.fail('scene equality must not coerce'); }}])
  for (const throwScene of [false, true])
    await compare({mode: 'main', scene: {}, sceneGetter: true, sceneValue, throwScene, failAt: 0});

for (const scene of [null, {scene: 'video'}, {scene: 'voice'}, {}])
  for (const reject of [undefined, 'raw', 'pixel', 'incoming'])
    for (const failAt of [0, 1, 2, 3, 5, 8, 12, 16, 20, 24])
      await compare({mode: 'main', scene, reject, failAt});
for (const input of [1, 2, 3, 4, 5, undefined, null, '1'])
  for (const reject of [undefined, 'change'])
    for (const failAt of [0, 1, 2, 3, 4, 5, 6, 8])
      await compare({mode: 'event', input, reject, failAt});
for (const mode of ['callback', 'close']) for (const input of [1, 3, 5])
  for (const noPixel of [false, true])
    await compare({mode, input, scene: {scene: 'video'}, noPixel, noInfo: true, callId: '', failAt: 0});
let scheduledCases = 0;
for (const input of [1, 2, 3, 4, 5, undefined, null, '1'])
  for (const concurrent of [false, true]) for (const reverse of [false, true])
    for (const failRequest of [-1, 0, 1, 2]) for (const errorGetter of ['none', 'code', 'message'])
      for (const replaceCallee of [false, true]) {
        const scenario = {input, concurrent, reverse, failRequest, errorGetter, replaceCallee};
        assert.deepEqual(await observeScheduled(recovered.source, scenario),
          await observeScheduled(paired, scenario), JSON.stringify(scenario));
        ++scheduledCases;
      }

// Ensure the oracle detects valid-source changes to suspension, live field
// reads and receiver. Mutations never enter the production recovery engine.
const scenario = {input: 1, concurrent: true, reverse: true, failRequest: -1,
  errorGetter: 'none', replaceCallee: true};
const expected = await observeScheduled(paired, scenario);
const eventStart = recovered.source.indexOf('static async processVoipCallEvent(');
const eventEnd = recovered.source.indexOf('static close(', eventStart);
assert.ok(eventStart >= 0 && eventEnd > eventStart);
const body = recovered.source.slice(eventStart, eventEnd);
const mutate = change => recovered.source.slice(0, eventStart) + change(body) + recovered.source.slice(eventEnd);
const stale = mutate(text => text.replaceAll('$local1.callId', '"stale-call"'));
assert.notEqual(stale, recovered.source);
assert.notDeepEqual(await observeScheduled(stale, scenario), expected);
const receiver = mutate(text => text.replaceAll('voipCall.reportCallStateChange(', '(0, voipCall.reportCallStateChange)('));
assert.notEqual(receiver, recovered.source);
await assert.rejects(() => observeScheduled(receiver, scenario), assert.AssertionError);
const eager = mutate(text => text.replace('(await $value3);', '(void $value3);'));
assert.notEqual(eager, recovered.source);
await assert.rejects(() => observeScheduled(eager, scenario), assert.AssertionError);
console.log(`${cases} paired VoipCallService scenarios and ${scheduledCases} controlled schedules passed; ` +
  'three semantic mutations detected; HarmonyOS runtime and complete source-style acceptance remain separate');
