import assert from 'node:assert/strict';
import {types} from 'node:util';

// Controlled platform boundaries only. Every application method and callback
// remains the real ESM body. Own native requests and derived then/catch chains;
// settle them explicitly rather than declaring completion after a tick count.
export function pushGraphPlatform(trace, config, token) {
  const requests = [], owned = [], completions = [], listeners = [], deliveries = [], windows = [], images = [];
  const hosts = new Map(), buffers = new Map(), bases = [], bindings = [], tracked = new WeakSet();
  let pulse;
  const changed = () => {const notify = pulse; pulse = undefined; if (notify) notify();};
  const rejection = trace.tag({code: config.code ?? 1600004, message: 'platform rejection'}, 'platform.rejection');
  const outcome = error => Object.is(error, rejection) ? 'platform rejection' : trace.outcome(error);
  const track = promise => {
    if (promise === undefined) return promise;
    assert.ok(types.isPromise(promise));
    if (tracked.has(promise)) return promise;
    tracked.add(promise); const entry = {promise, state: 'pending'}; owned.push(entry);
    Promise.prototype.then.call(promise, value => {entry.state = 'fulfilled'; entry.value = value; changed();},
      error => {entry.state = 'rejected'; entry.value = error; changed();});
    return promise;
  };
  const monitor = (promise, id) => {
    track(promise);
    return trace.surface('promise' + id, Object.fromEntries(['then', 'catch'].map(name => [name,
      (...callbacks) => monitor(Reflect.apply(Promise.prototype[name], promise, callbacks), id)])));
  };
  const request = (kind, result) => {
    const entry = {kind, id: requests.length, released: false}; requests.push(entry);
    const promise = new Promise((resolve, reject) => {entry.resolve = resolve; entry.reject = reject;});
    entry.result = result; const wrapper = monitor(promise, entry.id); changed();
    trace.event('platform.request', kind, entry.id); return wrapper;
  };
  const finish = async () => {
    while (owned.some(entry => entry.state === 'pending') || requests.some(entry => !entry.released)) {
      const available = requests.filter(entry => !entry.released);
      for (const entry of config.reverse ? available.reverse() : available) {
        entry.released = true; trace.trace.push(['platform.release', entry.kind, entry.id]);
        if (entry.kind === config.reject) entry.reject(rejection); else entry.resolve(entry.result);
      }
      // Observe actual request/settlement activity. An async parent may create
      // its next request after any number of native reactions, so neither ticks
      // nor an allSettled barrier on that pending parent can drive this queue.
      if (owned.some(entry => entry.state === 'pending')) await new Promise(resolve => {pulse = resolve;});
    }
    await Promise.allSettled(owned.map(entry => entry.promise));
    completions.splice(0, completions.length, ...owned.map(entry => entry.state === 'fulfilled'
      ? ['fulfilled', trace.normalize(entry.value)] : ['rejected', outcome(entry.value)]));
  };
  const resource = number => trace.surface('resource' + number, {
    getRawFileContent(name) {
      assert.equal(name, 'example.png'); const raw = new Uint8Array([number, 7]);
      buffers.set(raw.buffer, number); return request('raw', raw);
    },
    getMediaContent(id) {
      assert.equal(id, 16777260); const raw = new Uint8Array([number, 11]);
      buffers.set(raw.buffer, number); return request('media', raw);
    },
  });
  function Base(...args) {
    const owner = this; trace.tag(owner, new.target.name + ':base' + bases.length); bases.push(owner);
    trace.event('ability.super', new.target.name, ...args);
    const ui = trace.surface('ability.context' + bases.length, {
      resourceManager: resource(bases.length),
      getApplicationContext: () => trace.surface('application', {setColorMode: () => token}),
    });
    Object.defineProperty(owner, 'context', {configurable: true, get() {trace.event('ability.context.get', owner); return ui;}});
  }
  const pixel = number => {
    const value = trace.surface('pixel' + number, {then: undefined, getImageInfo(callback) {
      images.push({callback, number}); trace.event('platform.image.listener', number, callback); return token;
    }}); return value;
  };
  const load = (name, version = 0) => {
    const key = name + ':' + version; if (hosts.has(key)) return hosts.get(key);
    const surface = members => trace.surface(name + ':' + version, members);
    let host;
    switch (name) {
      case '@ohos:app.ability.UIAbility':
      case '@hms:core.push.RemoteNotificationExtensionAbility':
      case '@ohos:app.form.FormExtensionAbility':
      case '@ohos:application.BackupExtensionAbility':
        // SDK classes have distinct identities even when this controlled
        // protocol shares construction behavior. Aliasing them masks heritage.
        host = class extends Base {};
        trace.tag(host, name + ':' + version);
        trace.tag(host.prototype, name + ':' + version + ':prototype'); break;
      case '@ohos:hilog': host = surface({info: () => token, error: () => token}); break;
      case '@ohos:app.ability.ConfigurationConstant':
        host = surface({ColorMode: surface({COLOR_MODE_NOT_SET: 0})}); break;
      case '@ohos:notificationManager':
        host = surface({setBadgeNumber: () => request('badge', undefined),
          isNotificationEnabled: () => request('enabled', config.enabled ?? false),
          requestEnableNotification: () => request('enable', undefined)}); break;
      case '@hms:core.AAID':
        host = surface({getAAID: () => request('aaid', 'id:' + version),
          deleteAAID: () => request('delete', undefined)}); break;
      case '@hms:core.push.pushCommon':
        host = surface({AppProfileType: surface({PROFILE_TYPE_APPLICATION_ACCOUNT: 3})}); break;
      case '@hms:core.push.pushService':
        host = surface({getToken: () => request('token', 'token:' + version),
          bindAppProfileId: () => request('bind', undefined),
          unbindAppProfileId: () => request('unbind', undefined),
          receiveMessage(kind, owner, callback) {
            deliveries.push({kind, owner, callback}); trace.event('platform.delivery.listener', kind, owner, callback); return token;
          }}); break;
      case '@hms:telephony.voipCall':
        host = surface({
          VoipCallType: surface({VOIP_CALL_VOICE: 20, VOIP_CALL_VIDEO: 21}),
          VoipCallState: surface({VOIP_CALL_STATE_RINGING: 10, VOIP_CALL_STATE_ANSWERED: 11,
            VOIP_CALL_STATE_ACTIVE: 12, VOIP_CALL_STATE_DISCONNECTED: 13}),
          VoipCallUiEvent: surface({VOIP_CALL_EVENT_VOICE_ANSWER: 1, VOIP_CALL_EVENT_VIDEO_ANSWER: 2,
            VOIP_CALL_EVENT_REJECT: 3, VOIP_CALL_EVENT_HANGUP: 4}),
          on(name, callback) {assert.equal(name, 'voipCallUiEvent'); listeners.push(callback); trace.event('platform.voip.listener', callback); return token;},
          off: () => token, reportIncomingCall: () => request('incoming', 0),
          reportCallStateChange: () => request('change', undefined),
        }); break;
      case '@ohos:multimedia.image':
        host = surface({createImageSource(buffer) {
          const number = buffers.get(buffer) ?? -1; trace.event('platform.image', number, buffer.byteLength);
          return trace.surface('image.source' + images.length, {createPixelMap: () => request('pixel', config.noPixel ? null : pixel(number))});
        }}); break;
      case '@ohos:app.form.formInfo':
        host = surface({FormParam: surface({IDENTITY_KEY: 'id'}), FormState: surface({READY: 1})}); break;
      case '@ohos:app.form.formBindingData':
        host = surface({createFormBindingData(data) {const result = {data}; bindings.push(result); return result;}}); break;
      default: assert.fail('unmodeled platform import ' + name);
    }
    hosts.set(key, host); return host;
  };
  const window = (owner, open) => trace.surface('window' + windows.length, {loadContent(path, storage, callback) {
    if (typeof storage === 'function') {callback = storage; storage = undefined;}
    assert.equal(typeof callback, 'function'); windows.push({owner, path, storage, callback, open});
    trace.event('platform.window.listener', path, owner, storage, callback); return token;
  }});
  const replayWindows = () => {
    for (const entry of config.reverse ? [...windows].reverse() : windows) {
      trace.call('window.callback', entry.callback, {foreign: true}, [{code: config.windowCode ?? 0}]);
      if (!config.windowCode) entry.open(entry.path, entry.storage);
    }
  };
  const replay = () => {
    for (const {kind, owner, callback} of config.reverse ? [...deliveries].reverse() : deliveries) {
      const data = JSON.stringify({callId: 'call:' + bases.indexOf(owner), data: JSON.stringify({scene: config.scene ?? 'video'})});
      trace.call('push.callback', callback, {foreign: true}, [{get data() {trace.event('platform.payload', kind, owner); return data;}}]);
    }
    for (const {callback, number} of images) trace.call('image.callback', callback, {foreign: true},
      [null, config.noInfo ? null : {size: {width: 13 + number, height: 7}}]);
    for (const callback of listeners) track(trace.call('voip.callback', callback, {foreign: true}, [{voipCallUiEvent: config.input ?? 2}]));
  };
  return {load, finish, track, window, replayWindows, replay, bindings, requests,
    snapshot: () => ({completions, requests: requests.map(({kind, released}) => [kind, released]),
      deliveries: deliveries.map(({kind, owner}) => [kind, trace.normalize(owner)]),
      listeners: listeners.length, images: images.length, windows: windows.map(({path}) => path)})};
}
