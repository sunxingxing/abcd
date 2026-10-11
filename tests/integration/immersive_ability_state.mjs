import assert from 'node:assert/strict';
import vm from 'node:vm';
import {types} from 'node:util';
import {navigationWindow} from './navigation_window.mjs';

export const abilityName = 'default/src/main/ets/entryability/EntryAbility';
const utilName = 'commons/src/main/ets/utils/WindowUtil';
const lifecycle = ['onCreate', 'onForeground', 'onBackground', 'onWindowStageDestroy', 'onDestroy'];

// Execute both independent five-module graphs, including the actual constructor,
// WindowInfo and retained callbacks. Only platform services/scheduling are modeled.
export async function exerciseAbility(sources, config = {}, generated = false) {
  const trace = [], sites = [], callbacks = [], owners = [], utilities = [], storage = new Map();
  const modules = new Map(), imports = new Map(), tags = new WeakMap();
  const failure = [undefined, null, 0, Symbol('original failure'), {}][config.failureIndex ?? 4];
  let events = 0, failed = false, context;
  const tag = (value, name) => {tags.set(value, name); return value;};
  const normalize = value => {
    if (value === null || typeof value !== 'object' && typeof value !== 'function') return value;
    if (tags.has(value)) return tags.get(value);
    if (modules.get(utilName)?.status === 'evaluated' && value instanceof modules.get(utilName).namespace.WindowUtil) {
      track(value); return tags.get(value);
    }
    if (typeof value === 'function') return ['function', value.name, value.length, Object.hasOwn(value, 'prototype')];
    return Reflect.ownKeys(value).map(key => {
      const d = Object.getOwnPropertyDescriptor(value, key);
      assert.ok('value' in d, 'snapshot must not execute an accessor');
      return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
    });
  };
  const outcome = error => {
    if (Object.is(error, failure)) return 'original-throw';
    if (error instanceof assert.AssertionError) throw error;
    assert.ok(types.isNativeError(error) && ['TypeError', 'ReferenceError'].includes(error.name), String(error));
    return error.name;
  };
  const event = (name, ...args) => {
    sites.push([++events, name, ...args.map(normalize)]); trace.push([name, ...args.map(normalize)]);
    if (events === config.swapAt) {
      platform.swap(new Map([...imports].filter(([name]) => name !== '@ohos:app.ability.UIAbility')));
      imports.get('@ohos:app.ability.UIAbility').setExport('default', function WrongBase() {
        assert.fail('evaluated class reread its heritage');
      });
      trace.push(['live-import-swap']);
    }
    if (events === config.failAt) {failed = true; throw failure;}
  };
  const token = tag({get then() {assert.fail('void result was assimilated');}}, 'void');
  const call = (name, callback, receiver, args = []) => {
    try {assert.equal(Reflect.apply(callback, receiver, args), undefined); trace.push([name + '.return']); return true;}
    catch (error) {trace.push([name + '.throw', outcome(error)]); return false;}
  };
  const surface = (name, members) => {
    let owner;
    owner = new Proxy({}, {get(_, key, receiver) {
      assert.equal(receiver, owner); assert.ok(Object.hasOwn(members, key), name + '.' + String(key));
      event(name + '.get', key);
      const value = members[key];
      if (typeof value !== 'function') return value;
      const callback = function(...args) {
        assert.equal(this, owner); event(name + '.call', key, ...args); return value(...args);
      };
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callback, key, {
        get() {assert.fail('ambient invocation adapter');},
      });
      return callback;
    }});
    return tag(owner, name);
  };
  const platform = navigationWindow({config, event, surface, tag, token, call, normalize, outcome});
  const track = value => {
    if (!value || utilities.includes(value)) return;
    assert.ok(value instanceof modules.get(utilName).namespace.WindowUtil);
    const number = utilities.length; utilities.push(value);
    tag(value, 'util' + number); tag(value.mainWindowInfo, 'info' + number);
    for (const key of ['onStatusTypeChange', 'onWindowButtonRectChange', 'onWindowSizeChange', 'onAvoidAreaChange']) {
      // Same explicit lowering contract as window_record.mjs: pinned property
      // assignments keep empty arrow names; native TS fields infer the key.
      assert.equal(value[key].name, generated ? '' : key);
      assert.equal(value[key].length, 1); assert.equal(Object.hasOwn(value[key], 'prototype'), false);
      tag(value[key], 'util' + number + ':' + key);
    }
  };
  function Base(...args) {
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    tag(owner, 'ability' + owners.length); owners.push(owner);
    event('ability.base', new.target.name, ...args);
    return config.baseObject ? owner : undefined;
  }
  const nativeJSON = JSON.stringify;
  context = vm.createContext({
    WidthBreakpoint: surface('width', {WIDTH_XS: 'xs', WIDTH_SM: 'sm', WIDTH_MD: 'md', WIDTH_LG: 'lg', WIDTH_XL: 'xl'}),
    HeightBreakpoint: surface('height', {HEIGHT_SM: 'small'}), Observed: Type => Type,
    AppStorage: surface('storage', {setOrCreate(key, value) {
      track(value); storage.set(key, value); event('ability.store', key, value); return token;
    }}),
    JSON: surface('json', {stringify(value) {
      if (config.json === 'throw') throw failure;
      if (config.json === 'undefined') return undefined;
      if (config.json === 'null') return null;
      if (config.json === 'false') return false;
      if (config.json === 'zero') return 0;
      return nativeJSON(value);
    }}),
  });
  for (const [name, source] of Object.entries(sources)) modules.set(name, new vm.SourceTextModule(source, {context, identifier: name}));
  const root = modules.get(abilityName);
  await root.link(specifier => {
    const name = specifier.startsWith('@normalized:N&&&') ? specifier.slice('@normalized:N&&&'.length).split('&')[0] : specifier;
    if (modules.has(name)) return modules.get(name);
    assert.ok(name.startsWith('@ohos:'), 'missing real dependency ' + name);
    if (!imports.has(name)) imports.set(name, new vm.SyntheticModule(['default'], function() {
      this.setExport('default', name === '@ohos:app.ability.UIAbility' ? Base : platform.load(name));
    }, {context}));
    return imports.get(name);
  });
  assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
  await root.evaluate();
  assert.deepEqual(Object.keys(root.namespace), ['default']);
  const Type = root.namespace.default, methodNames = [...lifecycle, 'onWindowStageCreate'];
  assert.equal(Type.name, 'EntryAbility'); assert.equal(Type.length, 0);
  assert.equal(Object.getPrototypeOf(Type), Base);
  assert.equal(Reflect.set(root.namespace, 'default', null), false);
  assert.throws(() => Reflect.apply(Type, null, []), {name: 'TypeError'});
  for (const name of methodNames) {
    const d = Object.getOwnPropertyDescriptor(Type.prototype, name);
    assert.equal(d.enumerable, false); assert.equal(d.writable, true); assert.equal(d.configurable, true);
    assert.equal(d.value.name, name); assert.equal(d.value.length, name === 'onWindowStageCreate' ? 1 : 0);
    assert.equal(Object.hasOwn(d.value, 'prototype'), false);
  }
  const before = events; await root.evaluate(); assert.equal(events, before);
  const assigned = new WeakMap();
  if (config.windowSetter) Object.defineProperty(Type.prototype, 'windowUtil', {
    configurable: true,
    get() {event('ability.window.get', this); return assigned.get(this);},
    set(value) {
      track(value); event('ability.window.set', this, value);
      if (config.windowSetter === 'throw') throw failure;
      assigned.set(this, value);
    },
  });
  function Alternate() {}
  Object.setPrototypeOf(Alternate.prototype, Type.prototype);
  const Target = config.alternate ? Alternate : Type;
  const states = [];
  const snapshot = (label, owner) => {
    track(owner.windowUtil);
    states.push([label, normalize(Reflect.ownKeys(owner).map(key => [key, Object.getOwnPropertyDescriptor(owner, key)]))]);
  };
  for (let number = 0; number < 2; ++number) {
    let owner;
    try {owner = Reflect.construct(Type, ['argument' + number, null, 17], Target);}
    catch (error) {trace.push(['construct.throw', outcome(error)]); continue;}
    assert.equal(owner, owners.at(-1)); assert.equal(Object.getPrototypeOf(owner), Target.prototype);
    snapshot('constructed', owner);
    for (const name of ['onCreate', 'onForeground']) call(name, owner[name], owner);
    for (let creation = 0; creation < 2; ++creation) {
      const id = callbacks.length;
      const stage = surface(`stage${number}:${creation}`, {
        getMainWindowSync: () => platform.mainWindows[creation],
        loadContent(path, callback) {
          assert.equal(path, 'pages/Index'); assert.equal(callback.length, 1); assert.equal(Object.hasOwn(callback, 'prototype'), false);
          const entry = {id, owner, callback, number, creation}; callbacks.push(entry);
          event('ability.retained', id, callback);
          if (config.immediate) invoke(entry);
          return token;
        },
      });
      call('onWindowStageCreate', owner.onWindowStageCreate, owner, [stage]);
      snapshot('created:' + creation, owner);
    }
    for (const name of ['onBackground', 'onWindowStageDestroy', 'onDestroy']) call(name, owner[name], owner);
  }
  function invoke(entry) {
    const error = tag({get code() {event('ability.code', entry.id); return config.code ?? 0;}}, 'load-error:' + entry.id);
    return Reflect.apply(entry.callback, {foreign: true}, [error]);
  }
  const replayed = [];
  for (let round = 0; round < 2; ++round) {
    for (const entry of config.reverse ? [...callbacks].reverse() : callbacks) {
      call('load.callback', () => invoke(entry), undefined);
      replayed.push(entry.id); snapshot('replay:' + round, entry.owner);
    }
  }
  await platform.update(); await platform.finish();
  return {trace, sites, events, failed, states, callbacks: callbacks.length, replayed,
    storage: [...storage].map(([key, value]) => [key, normalize(value)]),
    utilities: utilities.map(value => ({
      state: normalize(Reflect.ownKeys(value).map(key => [key, Object.getOwnPropertyDescriptor(value, key)])),
      info: normalize(Reflect.ownKeys(value.mainWindowInfo).map(key => [key, Object.getOwnPropertyDescriptor(value.mainWindowInfo, key)])),
    })), platform: platform.snapshot(), modules: [...modules].map(([name, module]) => [name, module.status])};
}
