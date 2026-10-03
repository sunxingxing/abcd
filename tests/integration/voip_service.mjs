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
const paired = stripTypeScriptTypes(readFileSync(join(root,
  'src/entry/src/main/ets/service/VoipCallService.ts'), 'utf8'));
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
  const context = vm.createContext({Uint8Array});
  const module = new vm.SourceTextModule(source, {context});
  const hosts = new Map([['@hms:telephony.voipCall', voip], ['@ohos:hilog', hilog], ['@ohos:multimedia.image', image]]);
  await module.link(specifier => {
    assert.ok(hosts.has(specifier));
    return new vm.SyntheticModule(['default'], function() { this.setExport('default', hosts.get(specifier)); }, {context});
  });
  await module.evaluate();
  const service = module.namespace.VoipCallService;
  assert.equal(service.callId, undefined);
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
let cases = 0;
const compare = async scenario => {
  assert.deepEqual(await observe(recovered.source, scenario), await observe(paired, scenario), JSON.stringify(scenario));
  ++cases;
};
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
console.log(`${cases} paired VoipCallService scenarios passed; HarmonyOS runtime and source-style acceptance remain separate`);
