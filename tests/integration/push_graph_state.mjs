import assert from 'node:assert/strict';
import vm from 'node:vm';
import {types} from 'node:util';
import {GraphTrace, realmIntrinsics, snapshotValues} from './graph_trace.mjs';
import {pushGraphPlatform} from './push_graph_platform.mjs';

const prefix = 'entry/src/main/ets/';
const components = ['Navigation', 'NavDestination', 'Flex', 'Row', 'Column', 'Button', 'Text', 'Image', 'Span', 'If'];
const enums = {FlexDirection: ['Column'], FlexAlign: ['SpaceBetween', 'Center'], GradientDirection: ['Top'],
  FontWeight: ['Bold'], Color: ['Black', 'Blue', 'White', 'Green', 'Red'], HorizontalAlign: ['Start'],
  FlexWrap: ['Wrap'], NavigationMode: ['Stack'], TextDecorationType: ['Underline'],
  ButtonType: ['Normal'], ButtonStyleMode: ['TEXTUAL']};
const failures = [undefined, null, 0, Symbol('original throw'), {}];

// Run actual record bodies together. Identity/descriptor snapshots, storage and
// child state, retained callbacks and request completion are independent facts.
export async function exercisePush(sources, config = {}) {
  const modules = new Map(), imports = new Map(), routes = new Map(), metadata = new WeakMap();
  const views = [], rendered = [], cells = [], callbacks = [], queue = [], childUpdates = [], navBuilders = [];
  const stores = [], storeValues = new WeakMap(), storage = new Map([['close', false]]);
  const watches = new WeakMap(), children = new Map(), platformTypes = [], formBindings = [];
  let context, platform, shared, activeOwner, activeObserver, nextElement = 0, nextWork = 0;
  const trace = new GraphTrace({...config, ...(config.failureIndex === undefined ? {} : {failure: failures[config.failureIndex]})},
    ordinal => {if (ordinal === config.swapAt) {
      for (const [name, dependency] of imports) dependency.setExport('default', platform.load(name, 1));
      trace.trace.push(['imports.swap']);
    }});
  const token = trace.tag({get then() {assert.fail('void host return assimilated');}}, 'void');
  const call = (label, fn, owner, args = []) => {
    const result = trace.call(label, fn, owner, args);
    if (types.isPromise(result)) platform.track(result); return result;
  };
  function LocalStorage() {
    const values = new Map();
    const object = trace.surface('local' + stores.length, {
      setOrCreate(key, value) {values.set(key, value); return token;},
      get(key) {return values.get(key);},
    });
    stores.push({object, values}); storeValues.set(object, values); return object;
  }
  LocalStorage.getShared = () => {trace.event('local.shared'); return shared;};
  shared = new LocalStorage();
  const changed = entry => {
    const callback = watches.get(entry.owner)?.get(entry.alias);
    if (callback) call('watch.' + entry.alias, callback, entry.owner, [entry.alias]);
  };
  const write = (map, key, value) => {
    const old = map.get(key); map.set(key, value);
    if (!Object.is(old, value)) for (const entry of cells)
      if (entry.map === map && entry.key === key && !entry.deleted) changed(entry);
  };
  const cell = (owner, key, value, alias = key, map) => {
    const entry = {owner, key, alias, value, map, deleted: false, purged: []};
    if (map && !map.has(key)) map.set(key, value);
    const object = trace.surface('cell' + cells.length, {
      get() {return map ? map.get(key) : entry.value;},
      set(value) {if (map) write(map, key, value); else {const old = entry.value; entry.value = value; if (!Object.is(old, value)) changed(entry);} return token;},
      reset(value) {entry.value = value; return token;},
      purgeDependencyOnElmtId(id) {entry.purged.push(id); return token;},
      aboutToBeDeleted() {entry.deleted = true; return token;},
    }); cells.push(entry); return object;
  };
  const enqueue = (kind, callback, owner, args = []) => {
    const entry = {kind, callback, owner, args}; queue.push(entry);
    trace.event('retained', kind, owner, callback);
    if (config.immediate) {queue.pop(); invoke(entry);}
  };
  const invoke = entry => {
    const previousOwner = activeOwner, previousObserver = activeObserver;
    activeOwner = entry.owner; activeObserver = entry.args[0];
    call('retained.' + entry.kind, entry.callback, {foreign: true}, entry.args);
    activeOwner = previousOwner; activeObserver = previousObserver;
  };
  const drain = () => {while (nextWork < queue.length) invoke(queue[nextWork++]);};
  function ViewPU(...args) {
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    trace.tag(owner, new.target.name + ':view' + views.length); views.push(owner);
    metadata.set(owner, {parent: args[0], local: storeValues.get(args[1]) ?? storeValues.get(shared), element: args[2]});
    if (args[0]) children.set(args[2], owner);
    trace.event('view.super', new.target.name, ...args);
    return config.baseObject ? owner : undefined;
  }
  trace.tag(ViewPU, 'ViewPU'); trace.tag(ViewPU.prototype, 'ViewPU.prototype');
  const methods = {
    finalizeConstruction() {return token;},
    createStorageLink(key, value, alias) {return cell(this, key, value, alias, storage);},
    createLocalStorageLink(key, value, alias) {return cell(this, key, value, alias, metadata.get(this).local);},
    createLocalStorageProp(key, value, alias) {return cell(this, key, value, alias, metadata.get(this).local);},
    addProvidedVar() {return token;},
    declareWatch(key, callback) {if (!watches.has(this)) watches.set(this, new Map()); watches.get(this).set(key, callback); return token;},
    observeComponentCreation2(callback, component) {
      const id = ++nextElement; const entry = {kind: 'observe', callback, owner: this, args: [id, true]};
      if (Object.hasOwn(component, 'name')) childUpdates.push({...entry, args: [id, false]});
      enqueue(entry.kind, callback, this, entry.args); return token;
    },
    ifElseBranchUpdateFunction(branch, callback) {call('branch.' + branch, callback, {foreign: true}); return token;},
    updateStateVarsOfChildByElmtId(id, params) {
      const child = children.get(id); if (child) call('child.update', child.updateStateVars, child, [params]); return token;
    },
    updateDirtyElements() {return token;},
    id__() {return views.indexOf(this);},
    aboutToBeDeletedInternal() {return token;},
    getUIContext() {return trace.surface('ui.context', {getRouter: () => trace.surface('router', {back: () => token})});},
  };
  for (const [name, body] of Object.entries(methods)) Object.defineProperty(ViewPU.prototype, name, {configurable: true, get() {
    const owner = this; assert.ok(metadata.has(owner)); trace.event('view.get', name, owner);
    return function(...args) {assert.equal(this, owner); trace.event('view.call', name, owner, ...args); return Reflect.apply(body, owner, args);};
  }});
  if (config.finalize === 'missing') delete ViewPU.prototype.finalizeConstruction;
  const render = owner => {
    if (typeof owner.aboutToAppear === 'function') call('view.appear', owner.aboutToAppear, owner);
    call('view.render', owner.initialRender, owner); rendered.push(owner);
  };
  ViewPU.create = owner => {trace.event('child.create', owner); render(owner); return token;};
  function NavPathStack() {return trace.surface('path' + views.length, {
    pushPathByName: () => token, pop: () => token,
  });}
  function Observed(value, owner, key) {trace.event('observed.new', owner, key, value); return cell(owner, key, value);}
  const sandbox = {ViewPU, LocalStorage, NavPathStack, Uint8Array,
    ObservedPropertySimplePU: Observed, ObservedPropertyObjectPU: Observed,
    SubscriberManager: trace.surface('subscriber', {Get: () => trace.surface('subscriber.set', {delete: () => token})}),
    AppStorage: trace.surface('app.storage', {setOrCreate: (key, value) => {write(storage, key, value); return token;}}),
    ViewStackProcessor: trace.surface('view.stack', {AllocateNewElmetIdForNextComponent: () => ++nextElement,
      StartGetAccessRecordingFor: () => token, StopGetAccessRecording: () => token}),
    registerNamedRoute(factory, name, info) {routes.set(info.pagePath, factory); trace.event('route', info.pagePath, name, factory); return token;},
    loadEtsCard(owner, label) {enqueue('card', () => render(owner), owner); trace.event('card', label, owner); return token;},
    postCardAction(owner, action) {trace.event('card.action', owner, action); return token;},
    canIUse(name) {trace.event('capability', name); return config.capability ?? true;},
  };
  for (const [name, keys] of Object.entries(enums)) sandbox[name] = trace.surface(name,
    Object.fromEntries(keys.map(key => [key, name + '.' + key])));
  for (const name of components) {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); trace.event('ui.get', name, key);
      return function(...args) {
        assert.equal(this, object); trace.event('ui.call', name, key, ...args);
        if (key === 'onClick') {callbacks.push({callback: args[0], owner: activeOwner});
          trace.event('ui.callback', name + ':' + activeOwner.constructor.name, activeOwner, args[0]);}
        if (name === 'NavDestination' && key === 'create' && typeof args[0] === 'function') enqueue('destination', args[0], activeOwner);
        if (name === 'Navigation' && key === 'navDestination') navBuilders.push({builder: args[0].builder, owner: activeOwner});
        if (key === 'title' && args[0]?.builder) enqueue('title', args[0].builder, activeOwner);
        return token;
      };
    }}); sandbox[name] = trace.tag(object, name);
  }
  context = vm.createContext(sandbox); platform = pushGraphPlatform(trace, config, token);
  const intrinsics = realmIntrinsics(context);
  // Observe each exact native reaction result, including a throwing .catch on
  // a real service promise discarded by its caller. Keep native receiver,
  // arguments and returned Promise; never substitute an application body.
  platform.observeReactions(vm.runInContext('Promise.prototype', context));
  for (const [name, source] of Object.entries(sources)) modules.set(name, new vm.SourceTextModule(source, {context, identifier: name}));
  const dependency = name => {
    if (name.startsWith('@normalized:N&&&')) {
      const path = name.slice('@normalized:N&&&'.length, -1); assert.ok(modules.has(path), name); return modules.get(path);
    }
    if (!imports.has(name)) imports.set(name, new vm.SyntheticModule(['default'], function() {this.setExport('default', platform.load(name));}, {context}));
    return imports.get(name);
  };
  for (const module of modules.values()) if (module.status === 'unlinked') await module.link(dependency);
  for (const module of modules.values()) {
    try {await module.evaluate();} catch (error) {trace.trace.push(['module.throw', module.identifier, trace.outcome(error)]);}
  }
  const exported = trace.exports(modules);
  const heritage = [...modules].flatMap(([name, module]) => module.status === 'evaluated'
    ? Object.entries(module.namespace).filter(([, value]) => typeof value === 'function' && value.prototype)
      .map(([key, Type]) => [name, key, trace.normalize(Object.getPrototypeOf(Type)),
        trace.normalize(Object.getPrototypeOf(Type.prototype))]) : []);
  // Capture the exact native async returns from real service methods, including
  // close's intentionally discarded invocation. Implementation stays unchanged.
  const service = modules.get(prefix + 'service/VoipCallService').namespace.VoipCallService;
  for (const name of ['processVoIPMainMsg', 'processVoipCallEvent']) {
    const original = service[name];
    service[name] = function(...args) {const result = Reflect.apply(original, this, args); platform.track(result); return result;};
  }
  const open = (path, local) => {
    if (!routes.has(path)) return;
    if (local) shared = local;
    const owner = call('route.factory', routes.get(path), {foreign: true});
    if (owner) render(owner);
  };
  const want = {parameters: config.parameters === 'absent' ? undefined : config.parameters === 'null' ? null
    : {testKey: Object.hasOwn(config, 'value') ? config.value : 'initial', id: 'form:0'}};
  for (const [name, module] of modules) {
    const Type = module.namespace.default;
    if (typeof Type !== 'function' || !name.includes('Ability')) continue;
    for (let instance = 0; instance < (config.instances ?? 1); ++instance) {
      let owner;
      try {owner = new Type('argument'); platformTypes.push([name, owner]);}
      catch (error) {trace.trace.push(['ability.construct.throw', name, trace.outcome(error)]); continue;}
      for (const method of ['onCreate', 'onNewWant', 'onForeground', 'onBackground', 'onConfigurationUpdate',
        'onBackup', 'onRestore', 'onReceiveMessage', 'onAddForm', 'onAcquireFormState', 'onCastToNormalForm',
        'onUpdateForm', 'onFormEvent', 'onRemoveForm']) if (typeof owner[method] === 'function') {
        const args = method === 'onConfigurationUpdate' ? [{language: config.language ?? 'zh-CN'}]
          : method === 'onRestore' ? [{versionCode: 3, versionName: '3'}]
          : method.includes('Form') && method !== 'onAddForm' && method !== 'onAcquireFormState' ? ['form:0', 'event']
          : [want, {}];
        const result = call('ability.' + method, owner[method], owner, args);
        if (method === 'onAddForm' && result) formBindings.push(result);
      }
      if (typeof owner.onWindowStageCreate === 'function')
        call('ability.window', owner.onWindowStageCreate, owner, [platform.window(owner, open)]);
    }
  }
  platform.replayWindows(); drain();
  for (const entry of navBuilders) for (const name of ['pushToken', 'ExamplePage', 'unknown']) {
    activeOwner = entry.owner; call('navigation.builder', entry.builder, {foreign: true}, [name, {}]); drain();
  }
  activeOwner = undefined;
  await platform.finish(); platform.replay(); await platform.finish();
  // Public view APIs are observable even when the sample's render tree does
  // not call them. Keep this scenario separate from the existing boundary
  // fault matrix, preserving its exact event ordinals and previous evidence.
  const publicCalls = {getters: 0, setters: 0, entryNames: 0, builders: 0, callbacks: 0};
  if (config.publicApi) {
    for (const owner of [...views]) {
      const Type = owner.constructor;
      for (const key of Reflect.ownKeys(Type.prototype)) {
        const descriptor = Object.getOwnPropertyDescriptor(Type.prototype, key);
        if (!descriptor.get) continue;
        const original = call('public.get.' + String(key), descriptor.get, owner); ++publicCalls.getters;
        if (descriptor.set) {
          call('public.set.' + String(key), descriptor.set, owner, [config.publicValue]); ++publicCalls.setters;
          call('public.readback.' + String(key), descriptor.get, owner);
          call('public.restore.' + String(key), descriptor.set, owner, [original]);
        }
      }
      if (Object.hasOwn(Type, 'getEntryName')) {
        call('public.entryName', Type.getEntryName, Type); ++publicCalls.entryNames;
      }
      if (Object.hasOwn(Type.prototype, 'CustomButton')) {
        activeOwner = owner;
        const callback = trace.tag(function(...args) {
          ++publicCalls.callbacks; trace.event('public.button.callback', this, ...args); return token;
        }, 'public.button.callback');
        call('public.CustomButton', owner.CustomButton, owner, [config.publicValue, callback]);
        ++publicCalls.builders; drain(); activeOwner = undefined;
      }
    }
    await platform.finish();
  }
  const originalCallbacks = [...callbacks];
  for (const entry of config.reverse ? originalCallbacks.reverse() : originalCallbacks)
    call('button.callback', entry.callback, {foreign: true}, []);
  await platform.finish();
  // Form-provider output feeds the actual widget's local-storage consumer.
  for (const entry of cells) if (entry.owner.constructor.name === 'WidgetCard' && entry.map)
    write(entry.map, entry.key, entry.key === 'image_key' ? config.image ?? 'new-image'
      : formBindings[0]?.data?.[entry.key] ?? 'updated');
  for (const entry of childUpdates) invoke(entry);
  for (const owner of [...views]) {
    if (typeof owner.paramsGenerator_ === 'function') call('params.generator', owner.paramsGenerator_, {foreign: true});
    if (owner.constructor.name === 'WidgetCard' || owner.constructor.name === 'GetTokenPage')
      call('view.render.updated', owner.initialRender, owner);
    for (const method of ['onPageShow', 'onPageHide', 'rerender', 'purgeVariableDependenciesOnElmtId',
      'aboutToDisappear', 'aboutToBeDeleted']) if (typeof owner[method] === 'function')
      call('view.' + method, owner[method], owner, [17]);
  }
  drain(); await platform.finish();
  for (const [, owner] of platformTypes) for (const method of ['onWindowStageDestroy', 'onDestroy'])
    if (typeof owner[method] === 'function') call('ability.' + method, owner[method], owner);
  const fields = owner => Reflect.ownKeys(owner).map(key => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, key);
    return 'value' in descriptor ? [key, trace.normalize(descriptor.value), descriptor.writable, descriptor.enumerable, descriptor.configurable]
      : [key, trace.normalize(descriptor.get), trace.normalize(descriptor.set), descriptor.enumerable, descriptor.configurable];
  });
  return {exported, heritage, trace: trace.trace, sites: trace.sites, events: trace.events, failed: trace.failed,
    diagnostics: trace.errors.length, modules: [...modules].map(([name, module]) => [name, module.status]),
    views: views.map(owner => [owner.constructor.name, fields(owner)]), rendered: rendered.map(owner => owner.constructor.name),
    abilities: platformTypes.map(([name, owner]) => [name, fields(owner)]),
    cells: cells.map(entry => [trace.normalize(entry.owner), entry.alias,
      trace.normalize(entry.map ? entry.map.get(entry.key) : entry.value), entry.deleted, entry.purged]),
    stores: stores.map(({values}) => [...values].map(([key, value]) => [key, trace.normalize(value)])),
    storage: [...storage], forms: formBindings.map(fields), platform: platform.snapshot(), callId: service.callId,
    ...(config.publicApi ? {publicCalls} : {}),
    identities: snapshotValues({views, abilities: platformTypes, forms: formBindings,
      cells: cells.map(entry => [entry.owner, entry.alias, entry.map ? entry.map.get(entry.key) : entry.value]),
      stores: stores.map(({values}) => [...values]), storage: [...storage],
      exports: [...modules].filter(([, module]) => module.status === 'evaluated').map(([name, module]) => [name, module.namespace])}, intrinsics)};
}
