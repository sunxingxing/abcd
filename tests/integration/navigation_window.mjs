import assert from 'node:assert/strict';

const moduleName = 'commons/src/main/ets/utils/WindowUtil';
const callbacks = ['onStatusTypeChange', 'onWindowButtonRectChange', 'onWindowSizeChange', 'onAvoidAreaChange'];

// Test-only platform adapter. Store real exported WindowUtil instances in the
// shared cells; never replace WindowUtil/WindowInfo, methods or retained arrows.
// Promise ownership includes every derived chain, including callback failures.
export function navigationWindow({config, event, surface, tag, token, call, normalize, outcome}) {
  const instances = [], listeners = [], requests = [], owned = [], completions = [];
  const services = new Map(), replayedCallbacks = [];
  let phase = 0, replayed = 0, replayCandidates = 0;
  const error = tag({code: 17, message: 'controlled platform rejection'}, 'platform.rejection');
  const monitor = (promise, id) => {
    owned.push(promise); promise.then(() => {}, () => {});
    let wrapper;
    wrapper = surface('platform.promise' + id, Object.fromEntries(['then', 'catch'].map(name => [name, callback =>
      monitor(Promise.prototype[name].call(promise, callback), id)])));
    return wrapper;
  };
  const request = () => {
    const entry = {id: requests.length}; requests.push(entry);
    const result = monitor(new Promise((resolve, reject) => {entry.resolve = resolve; entry.reject = reject;}), entry.id);
    event('platform.request', entry.id); return result;
  };
  const status = surface('platform.status', {UNDEFINED: -1, MAXIMIZE: 1, FLOATING: 2, FULL_SCREEN: 3});
  const avoid = surface('platform.avoid', {TYPE_SYSTEM: 0, TYPE_CUTOUT: 1, TYPE_SYSTEM_GESTURE: 2, TYPE_KEYBOARD: 3, TYPE_NAVIGATION_INDICATOR: 4});
  const area = number => {
    const members = {};
    for (const [index, key] of ['topRect', 'leftRect', 'rightRect', 'bottomRect'].entries()) members[key] =
      surface(`platform.rect${number}:${key}`, {height: 10 + index + phase, width: 20 + index + phase, top: 30 + index, left: 40 + index});
    return surface('platform.area' + number, members);
  };
  // Independent platform results and a fresh event payload must retain their
  // own identities. Reusing one object for every area masked a wrong-field
  // mutation: overwriting avoidSystem then had no observable change.
  const regions = [0, 1].map(number => new Map([0, 1, 2, 3, 4].map(kind => [kind, area(number + ':' + kind)])));
  const updates = [0, 1].map(number => area('update' + number));
  const titles = [0, 1].map(number => tag({width: 30 + number, height: 10}, 'platform.title' + number));
  const ui = [0, 1].map(number => surface('platform.ui' + number, {
    px2vp: value => value / 2,
    getWindowWidthBreakpoint: () => phase ? config.nextWidth ?? 'xl' : config.width ?? 'sm',
    getWindowHeightBreakpoint: () => phase ? 'large' : 'medium',
  }));
  const main = [0, 1].map(number => surface('platform.main' + number, {
    getUIContext: () => ui[number], getWindowStatus: () => config.status ?? 3,
    getWindowProperties: () => ({windowRect: {width: 700 + number + phase, height: 900 + number}}),
    getWindowAvoidArea: kind => regions[number].get(kind), getTitleButtonRect: () => titles[number],
    on(name, callback) {
      assert.ok(['windowSizeChange', 'avoidAreaChange', 'windowTitleButtonRectChange', 'windowStatusChange'].includes(name));
      assert.equal(typeof callback, 'function');
      const listener = {number, name, callback}; listeners.push(listener);
      // Retain before the host's observable registration completion. A throw
      // here must not erase this exact callback or its captured instance.
      event('platform.listener', number, name, callback); return token;
    },
    off: () => token, setWindowDecorVisible: () => token, setWindowDecorHeight: () => token,
    setDecorButtonStyle: () => token, setWindowSystemBarEnable: request, recover: request,
    setPreferredOrientation: request,
  }));
  const load = (name, version = 0) => {
    const key = name + ':' + version;
    if (services.has(key)) return services.get(key);
    let value;
    if (name === '@ohos:window') value = surface('platform.window' + version, {
      WindowStatusType: status, AvoidAreaType: avoid, Orientation: surface('platform.orientation', {UNSPECIFIED: 0}),
    });
    else if (name === '@ohos:display') value = surface('platform.display' + version, {getDefaultDisplaySync: () => ({width: 960, height: 1280})});
    else if (name === '@ohos:hilog') value = surface('platform.hilog' + version, {info: () => undefined, error: () => undefined});
    else value = tag({}, name + version);
    services.set(key, value); return value;
  };
  const initialize = modules => {
    const Type = modules.get(moduleName).namespace.WindowUtil;
    for (const number of [0, 1]) {
      let owner;
      try {owner = new Type(main[number]);}
      catch (error) {event('platform.construct.throw', outcome(error)); continue;}
      tag(owner, 'window' + number); tag(owner.mainWindowInfo, 'info' + number);
      for (const key of callbacks) {
        assert.equal(owner[key].length, 1); assert.equal(Object.hasOwn(owner[key], 'prototype'), false);
        tag(owner[key], `platform.callback${number}:${key}`);
      }
      instances.push(owner);
      call('platform.context', owner.setUIContext, owner);
      call('platform.info', owner.updateWindowInfo, owner);
      call('platform.status.start', owner.startWindowStatusListener, owner);
      call('platform.orientation', owner.setWindowOrientation, owner, [13 + number]);
    }
    return instances;
  };
  const update = async () => {
    phase = 1;
    const entries = config.reverse ? [...listeners].reverse() : [...listeners];
    replayCandidates = entries.length;
    for (const entry of entries) {
      const {number, name, callback} = entry;
      const args = name === 'windowSizeChange' ? [{width: 840 + number, height: 1200}]
        : name === 'avoidAreaChange' ? [{type: config.areaType ?? 1, area: updates[number]}]
        : name === 'windowTitleButtonRectChange' ? [titles[1 - number]] : [config.nextStatus ?? 2];
      try {
        const result = Reflect.apply(callback, {foreign: true}, args);
        if (name === 'windowSizeChange') {
          assert.equal(Object.prototype.toString.call(result), '[object Promise]');
          await result;
        } else assert.equal(result, undefined);
        ++replayed; replayedCallbacks.push(normalize(callback)); event('platform.callback.return', number, name, callback);
      } catch (error) {event('platform.callback.throw', number, name, outcome(error));}
    }
  };
  const finish = async () => {
    for (const entry of config.reverse ? [...requests].reverse() : requests) {
      if (config.reject) entry.reject(error); else entry.resolve(undefined);
    }
    // allSettled observes callbacks that append derived chains; each chain is
    // owned before it is exposed to source. No fixed microtask/tick heuristic.
    let first = 0;
    while (first < owned.length) {
      const batch = owned.slice(first); first = owned.length;
      for (const result of await Promise.allSettled(batch)) completions.push(result.status === 'fulfilled'
        ? ['fulfilled', normalize(result.value)] : ['rejected', Object.is(result.reason, error) ? 'platform.rejection' : outcome(result.reason)]);
    }
  };
  const snapshot = () => ({instances: instances.map(owner => ({
    owner: normalize(Reflect.ownKeys(owner).map(key => [key, Object.getOwnPropertyDescriptor(owner, key)])),
    info: normalize(Reflect.ownKeys(owner.mainWindowInfo).map(key => [key, Object.getOwnPropertyDescriptor(owner.mainWindowInfo, key)])),
  })), listeners: listeners.map(({number, name, callback}) => [number, name, normalize(callback)]), replayed, replayCandidates, replayedCallbacks,
    requests: requests.length, completions});
  const swap = imports => {
    for (const [name, module] of imports) module.setExport('default', load(name, 1));
  };
  return {initialize, update, finish, snapshot, load, swap};
}
