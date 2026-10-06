import assert from 'node:assert/strict';
import vm from 'node:vm';
import {isDeepStrictEqual, inspect} from 'node:util';
import {GraphTrace} from './graph_trace.mjs';

const rootName = 'phone/src/main/ets/pages/MainPage';
const destinations = ['mainPage', 'rankPage', 'detailPage', 'pictureDetail', 'unknown'];
const componentNames = ['Blank', 'Button', 'Column', 'Divider', 'FlowItem', 'ForEach', 'Gesture',
  'Grid', 'GridCol', 'GridItem', 'GridRow', 'If', 'Image', 'List', 'ListItem', 'ListItemGroup',
  'NavDestination', 'Navigation', 'PinchGesture', 'Row', 'Scroll', 'SideBarContainer', 'Span',
  'Stack', 'Swiper', '__Common__', 'TabContent', 'Tabs', 'Text', 'TextArea', 'TextInput', 'WaterFlow'];
const enumNames = ['Alignment', 'Axis', 'BarMode', 'BarPosition', 'BarState', 'Color', 'CopyOptions',
  'EdgeEffect', 'FlexAlign', 'FontWeight', 'GesturePriority', 'HorizontalAlign', 'ImageFit',
  'ListItemAlign', 'MouseAction', 'MouseButton', 'NavigationMode', 'NestedScrollMode',
  'SafeAreaType', 'SideBarPosition', 'TextAlign', 'TextOverflow', 'VerticalAlign', 'Visibility'];

// The entire generated graph and independent TS graph run in distinct realms.
// Only ArkUI/storage/platform services are controlled; each real exported view,
// model, barrel, getter and retained callback remains the actual module body.
export async function exerciseCommunity(sources, config = {}) {
  let context, factory, nextElement = 0, activeElement, activeObserver, retainedFault, replayedFault = false;
  const owners = [], rendered = [], cells = [], retained = [], updates = [], callbacks = [], builders = [], tabBuilders = [];
  const modules = new Map(), imports = new Map(), children = new Map(), storage = new Map(Object.entries(config.storage ?? {}));
  const viewTypes = new Map(), itemIds = new WeakMap(), initializedLists = new WeakSet();
  let nextItem = 0;
  if (config.breakpoint !== undefined) storage.set('currentBreakpoint', config.breakpoint);
  const watches = new Map(), observers = new Map(), dirty = new Set(), branches = new Map(), uiState = new Map();
  const notify = (owner, key) => {
    const callback = watches.get(owner)?.get(key);
    if (callback) trace.call("watch." + key, callback, owner, [key]);
  };
  const trace = new GraphTrace(config, ordinal => {
    if (ordinal === config.swapAt) {
      for (const [name, value] of components[1]) context[name] = value;
      context.SubscriberManager = subscribers[1];
      context.ObservedPropertySimplePU = observed[1]; context.ObservedPropertyObjectPU = observed[1];
      trace.trace.push(['globals.swap']);
    }
  });
  const token = trace.tag({get then() {assert.fail('void host result assimilated');}}, 'void');
  const cell = (owner, key, value, linked = false) => {
    const entry = {owner, key, value, linked, dependencies: new Set(), purged: [], deleted: false};
    if (linked && !storage.has(key)) storage.set(key, value);
    const object = trace.surface('cell' + cells.length + ':' + key, {
      get() {if (activeObserver) entry.dependencies.add(activeObserver.id); return linked ? storage.get(key) : entry.value;},
      set(value) {
        const old = entry.value; entry.value = value;
        if (linked) writeStorage(key, value);
        else if (!Object.is(old, value)) changed(entry);
        return token;
      },
      reset(value) {entry.value = value; return token;},
      purgeDependencyOnElmtId(id) {entry.purged.push(id); entry.dependencies.delete(id); return token;},
      aboutToBeDeleted() {entry.deleted = true; entry.dependencies.clear(); return token;},
    });
    cells.push(entry); return object;
  };
  const changed = entry => {
    for (const id of entry.dependencies) dirty.add(id);
    notify(entry.owner, entry.key);
  };
  const writeStorage = (key, value) => {
    const old = storage.get(key); storage.set(key, value);
    if (!Object.is(old, value)) for (const entry of cells)
      if (entry.linked && entry.key === key && !entry.deleted) changed(entry);
  };
  function ViewPU(...args) {
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    trace.tag(owner, new.target.name + ':' + owners.length); owners.push(owner);
    if (owners.includes(args[0])) children.set(args[2], owner);
    trace.event('super', new.target.name, ...args);
    return config.baseObject ? owner : undefined;
  }
  const enqueue = (kind, callback, args = [], id = ++nextElement, owner) => {
    assert.equal(typeof callback, 'function');
    const entry = {kind, callback, args, id, owner}; retained.push(entry);
    if (kind === 'observe') observers.set(id, entry);
    if (trace.events + 1 === config.failAt) retainedFault = id;
    trace.event('retained', kind, id, callback);
    if (config.immediate) {
      retained.splice(retained.indexOf(entry), 1);
      invoke(entry);
    }
  };
  const invoke = entry => {
    const before = activeElement, previous = activeObserver; activeElement = entry.id;
    activeObserver = observers.has(entry.id) ? entry : undefined;
    trace.call('retained.' + entry.kind, entry.callback, {foreign: true}, entry.args);
    if (entry.id === retainedFault) replayedFault = trace.trace.at(-1)[0] === 'retained.' + entry.kind + '.return';
    activeElement = before; activeObserver = previous;
  };
  const methods = {
    createStorageLink(key, initial, alias) {
      assert.equal(key, alias);
      return cell(this, key, initial, true);
    },
    createStorageProp(key, initial, alias) {assert.equal(key, alias); return cell(this, key, initial, true);},
    observeComponentCreation2(callback, component) {
      assert.equal(callback.length, 2); assert.equal(Object.hasOwn(callback, 'prototype'), false);
      const id = ++nextElement, args = [id, config.initial ?? true];
      if (Object.hasOwn(component, 'name')) updates.push({kind: 'child.update', callback, args: [id, false], id});
      enqueue('observe', callback, args, id, this); return token;
    },
    ifElseBranchUpdateFunction(branch, callback) {
      trace.event('branch', this, branch);
      if (branches.get(activeElement) === branch) return token;
      branches.set(activeElement, branch);
      assert.equal(Reflect.apply(callback, {foreign: true}, []), undefined); return token;
    },
    forEachUpdateFunction(id, values, generator, key, indexed, keyIndexed) {
      trace.event('foreach', this, id, values, indexed, keyIndexed);
      for (let i = 0; i < values.length; ++i) {
        const item = values[i];
        if (item !== null && typeof item === 'object' && !itemIds.has(item)) {
          itemIds.set(item, nextItem); trace.tag(item, 'item' + nextItem++);
        }
        if (typeof key === 'function') trace.event('key', Reflect.apply(key, {foreign: true}, keyIndexed ? [item, i] : [item]));
        assert.equal(Reflect.apply(generator, {foreign: true}, indexed ? [item, i] : [item]), undefined);
      }
      return token;
    },
    updateStateVarsOfChildByElmtId(id, params) {
      trace.event('update.child', this, id, params);
      const child = children.get(id);
      if (child) trace.call('child.state', child.updateStateVars, child, [params]);
      return token;
    },
    declareWatch(key, callback) {
      if (!watches.has(this)) watches.set(this, new Map());
      watches.get(this).set(key, callback); return token;
    },
    id__() {return owners.indexOf(this) + 100;},
    aboutToBeDeletedInternal() {watches.delete(this); return token;},
    updateDirtyElements() {return token;},
  };
  for (const [name, body] of Object.entries(methods)) Object.defineProperty(ViewPU.prototype, name, {get() {
    const owner = this; assert.ok(owners.includes(owner)); trace.event('host.get', name, owner);
    return function(...args) {assert.equal(this, owner); trace.event('host.call', name, owner, ...args); return Reflect.apply(body, owner, args);};
  }});
  if (config.finalize === 'own' || config.finalize === undefined)
    ViewPU.prototype.finalizeConstruction = function() {trace.event('finalize', this); return token;};
  if (config.finalize === 'inherited') Object.setPrototypeOf(ViewPU.prototype, {
    finalizeConstruction() {trace.event('finalize.inherited', this); return token;},
  });
  if (config.finalize === 'noncallable') ViewPU.prototype.finalizeConstruction = token;
  if (config.finalize === 'frozen') Object.preventExtensions(ViewPU.prototype);
  trace.tag(ViewPU.prototype, 'ViewPU.prototype');
  const render = owner => {
    trace.call('view.appear', owner.aboutToAppear ?? (() => {}), owner);
    trace.call('view.render', owner.initialRender, owner); rendered.push(owner);
  };
  ViewPU.create = function(owner) {assert.equal(this, ViewPU); assert.ok(owners.includes(owner)); trace.event('view.create', owner); render(owner); return token;};
  function NavPathStack() {
    return trace.surface('path' + owners.length, {
      pushPath(path) {assert.equal(typeof path.name, 'string'); return token;},
      pushPathByName(name, param) {assert.equal(typeof name, 'string'); return token;},
      pop() {return token;},
      replacePath(path) {assert.equal(typeof path.name, 'string'); return token;},
      clear() {return token;},
    });
  }
  function NavPathInfo(name, param) {this.name = name; this.param = param;}
  const observed = [0, 1].map(version => function Observed(value, owner, key) {
    assert.ok(owners.includes(owner)); trace.event('observed.new', version, owner, key, value);
    return cell(owner, key, value);
  });
  const subscribers = [0, 1].map(version => trace.surface('subscriber' + version, {
    Get() {return trace.surface('subscriber.set' + version, {delete() {return token;}});},
  }));
  const uiMethod = (name, key, args) => {
    if (!uiState.has(activeElement)) uiState.set(activeElement, {});
    if (key === 'visibility' || key === 'enabled') uiState.get(activeElement)[key] = args[0];
    if (key === 'navDestination') builders.push(args[0].builder);
    if (key === 'tabBar') tabBuilders.push(args[0].builder);
    if (name === 'NavDestination' && key === 'create' || name === 'TabContent' && key === 'create')
      enqueue('content', args[0]);
    if (name === 'ListItem' && key === 'create' && typeof args[0] === 'function' && !initializedLists.has(args[0])) {
      initializedLists.add(args[0]); enqueue('list.item', args[0], [activeElement, config.initial ?? true]);
    }
    if (name === 'GridItem' && key === 'create' && typeof args[0] === 'function')
      enqueue('grid.item', args[0]);
    if (key === 'onClick' || key === 'onAppear' || key === 'onBackPressed' || key === 'onActionEnd')
      callbacks.push({kind: key, callback: args[0], args: [], id: activeElement});
    if (key === 'onChange') callbacks.push({kind: key, callback: args[0], args: [name === 'Tabs' ? 2 : 'comment']});
    if (key === 'onComplete') callbacks.push({kind: key, callback: args[0], args: [{width: 120, height: 60, componentWidth: 240, componentHeight: 120}]});
    if (key === 'onActionUpdate') callbacks.push({kind: key, callback: args[0], args: [{scale: 1.5}]});
    if (key === 'onMouse') callbacks.push({kind: key, callback: args[0], args: [{button: 'MouseButton.Left', action: 'MouseAction.Press'}]});
    if (key === 'onScrollFrameBegin') callbacks.push({kind: key, callback: args[0], args: [7, 'scroll']});
    return token;
  };
  const components = [0, 1].map(version => new Map(componentNames.map(name => {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); trace.event(name + version + '.get', key);
      return function(...args) {
        assert.equal(this, object); trace.event(name + version + '.call', key, ...args);
        return uiMethod(name, key, args);
      };
    }});
    return [name, trace.tag(object, name + version)];
  })));
  const enums = Object.fromEntries(enumNames.map(name => {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); trace.event(name + '.get', key); return name + '.' + String(key);
    }});
    return [name, trace.tag(object, name)];
  }));
  const appStorage = trace.surface('storage', {
    get(key) {return storage.get(key);},
    set(key, value) {writeStorage(key, value); return true;},
    setOrCreate(key, value) {writeStorage(key, value); return true;},
  });
  const scrollers = [];
  function Scroller() {
    const owner = trace.surface('scroller' + scrollers.length, {scrollToIndex() {return token;}});
    scrollers.push(owner); trace.event('scroller.new', owner); return owner;
  }
  function DigitIndicator() {
    let indicator;
    indicator = trace.surface('indicator', Object.fromEntries(
      ['fontColor', 'selectedFontColor', 'digitFont', 'selectedDigitFont', 'fontSize', 'selectedFontSize', 'bottom']
        .map(key => [key, () => indicator])));
    trace.event('indicator.new'); return indicator;
  }
  const makeBuilderParameterProxy = (name, providers) => {
    trace.event('builder.proxy', name, providers);
    let proxy;
    proxy = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, proxy); trace.event('builder.parameter', key);
      const provider = providers[key];
      return typeof provider === 'function' ? Reflect.apply(provider, undefined, []) : provider;
    }});
    return trace.tag(proxy, 'builder.parameters');
  };
  context = vm.createContext({ViewPU, NavPathStack, NavPathInfo, Scroller, DigitIndicator, makeBuilderParameterProxy,
    focusControl: trace.surface('focus', {requestFocus() {return token;}}),
    ObservedPropertySimplePU: observed[0], ObservedPropertyObjectPU: observed[0],
    AppStorage: appStorage, SubscriberManager: subscribers[0], ...Object.fromEntries(components[0]), ...enums,
    ViewStackProcessor: trace.surface('view.stack', {StartGetAccessRecordingFor() {return token;}, StopGetAccessRecording() {return token;}}),
    registerNamedRoute(callback, name, metadata) {assert.equal(this, undefined); factory = callback; trace.event('route', name, metadata); return token;},
  });
  for (const [name, source] of Object.entries(sources))
    modules.set(name, new vm.SourceTextModule(source, {context, identifier: name}));
  const link = specifier => {
    const name = specifier.startsWith('@normalized:N&&&') ? specifier.slice('@normalized:N&&&'.length).split('&')[0] : specifier;
    if (modules.has(name)) return modules.get(name);
    if (imports.has(name)) return imports.get(name);
    assert.ok(['@native:system.app', '@ohos:app.ability.UIAbility', '@ohos:deviceInfo', '@ohos:display', '@ohos:hilog'].includes(name), name);
    const platform = name === '@ohos:app.ability.UIAbility' ? class UIAbility {} :
      name === '@ohos:deviceInfo' ? trace.surface('device', {deviceType: config.deviceType ?? 'phone'}) :
      trace.surface(name, {info() {return undefined;}, debug() {return undefined;}, error() {return undefined;}, warn() {return undefined;},
        exit() {return undefined;}, setImageRawDataCacheSize() {return undefined;}, setImageCacheCount() {return undefined;}, getDefaultDisplaySync() {return {width: 960, height: 720};}});
    const module = new vm.SyntheticModule(['default'], function() {this.setExport('default', platform);}, {context});
    imports.set(name, module); return module;
  };
  const root = modules.get(rootName); await root.link(link);
  for (const module of modules.values()) if (module.status === 'unlinked') await module.link(link);
  let error;
  try {
    await root.evaluate();
    for (const module of modules.values()) if (module.status === 'linked') await module.evaluate();
    trace.trace.push(['module.return']);
  } catch (value) {error = value; trace.trace.push(['module.throw', trace.outcome(value)]);}
  const before = trace.events;
  if (error !== undefined || root.status === 'errored') await assert.rejects(root.evaluate(), value => Object.is(value, error));
  else await root.evaluate();
  assert.equal(trace.events, before);
  for (const [name, module] of modules) if (module.status === 'evaluated')
    for (const [key, Type] of Object.entries(module.namespace))
      if (typeof Type === 'function' && Object.getPrototypeOf(Type) === ViewPU) viewTypes.set(name + '#' + key, Type);
  const exportGraph = trace.exports(modules);
  const drain = () => {
    while (retained.length || dirty.size) {
      const entries = retained.splice(0);
      for (const id of dirty) {
        const observer = observers.get(id);
        if (observer) entries.push({...observer, kind: 'observe.update', args: [id, false]});
      }
      dirty.clear(); if (config.reverse) entries.reverse();
      for (const entry of entries) invoke(entry);
    }
  };
  if (factory) {
    const owner = trace.call('factory', factory, {foreign: true});
    if (owner) {
      viewTypes.set(rootName + '#route', owner.constructor);
      if (!config.constructOnly) render(owner);
      if (!config.constructOnly) trace.call('root.purge', owner.purgeVariableDependenciesOnElmtId, owner, [17]);
      if (!config.constructOnly) trace.call('root.rerender', owner.rerender, owner);
    }
  }
  if (config.constructOnly) {
    const parent = trace.tag({}, 'direct.parent'), params = trace.tag({}, 'direct.params');
    const local = trace.tag({}, 'direct.storage'), extra = trace.tag({}, 'direct.extra');
    const lambda = () => params;
    for (const Type of new Set(viewTypes.values())) {
      const args = [parent, params, local, 71, lambda, extra];
      args.length = config.argumentCount;
      if (config.argumentCount > 6) args.fill('extra argument', 6);
      const Alternate = function Alternate() {};
      Alternate.prototype = Object.create(Type.prototype);
      trace.call('direct.construct', () => Reflect.construct(Type, args, config.alternate ? Alternate : Type), undefined);
    }
  } else {
    drain();
    for (const builder of builders) for (const name of destinations)
      trace.call('destination', builder, {foreign: true}, [name, 0]);
    drain();
    for (const builder of tabBuilders) trace.call('tab.builder', builder, {foreign: true});
    drain();
    let exploredBuilders = builders.length, exploredTabs = tabBuilders.length;
    const lateBuilders = () => {
      while (exploredBuilders < builders.length || exploredTabs < tabBuilders.length) {
        while (exploredBuilders < builders.length)
          trace.call('destination.late', builders[exploredBuilders++], {foreign: true}, ['', 0]);
        while (exploredTabs < tabBuilders.length)
          trace.call('tab.builder.late', tabBuilders[exploredTabs++], {foreign: true});
        drain();
      }
    };
    for (const entry of [...callbacks]) {
      const state = uiState.get(entry.id);
      if (entry.kind === 'onClick' && (state?.visibility === 'Visibility.Hidden' ||
          state?.visibility === 'Visibility.None' || state?.enabled === false)) {
        trace.trace.push(['callback.hidden', entry.id]); continue;
      }
      trace.call('callback.' + entry.kind, entry.callback, {foreign: true}, entry.args);
      drain(); lateBuilders();
    }
    drain();
    for (const entry of updates) invoke(entry);
    for (const owner of owners) {
      if (Object.hasOwn(owner, 'paramsGenerator_')) {
        const params = trace.call('child.params', owner.paramsGenerator_, {foreign: true});
        trace.call('child.update', owner.updateStateVars, owner, [params]);
      }
      trace.call('view.purge', owner.purgeVariableDependenciesOnElmtId, owner, [17]);
      trace.call('view.rerender', owner.rerender, owner);
      trace.call('view.delete', owner.aboutToBeDeleted, owner);
    }
  }
  const names = new Map([...viewTypes].map(([key, Type]) => [Type, key]));
  return {exportGraph, retainedFault, replayedFault, diagnostics: trace.errors, trace: trace.trace, sites: trace.sites, events: trace.events, failed: trace.failed,
    modules: [...modules].map(([name, module]) => [name, module.status]),
    views: [...names.values()], rendered: rendered.map(owner => names.get(owner.constructor)), instances: owners.map(owner => names.get(owner.constructor)),
    state: owners.map(owner => trace.normalize(Reflect.ownKeys(owner).map(key => [key, Object.getOwnPropertyDescriptor(owner, key)]))),
    storage: trace.normalize([...storage]),
    cells: cells.map(entry => [trace.tags.get(entry.owner), entry.key, trace.normalize(entry.value), entry.purged, entry.deleted]),
    metadata: [...viewTypes].map(([key, Type]) => [key, Type.name, Type.length,
      Reflect.ownKeys(Type.prototype).map(name => [name, trace.normalize(Object.getOwnPropertyDescriptor(Type.prototype, name))])]),
  };
}

export function communityCases() {
  const cases = [];
  for (const breakpoint of ['sm', 'md', 'lg']) for (const immediate of [false, true])
    for (const isShowInput of [false, true]) for (const isFoldHorizontal of [false, true])
      cases.push({breakpoint, immediate, storage: {isShowInput, isFoldHorizontal}});
  for (const immediate of [false, true]) for (const reverse of [false, true])
    for (const baseObject of [false, true]) for (const finalize of ['own', 'absent', 'inherited'])
      cases.push({immediate, reverse, baseObject, finalize, breakpoint: 'lg', storage: {isShowInput: true}});
  for (const immediate of [false, true]) for (const reverse of [false, true]) for (const baseObject of [false, true])
    cases.push({immediate, reverse, baseObject, initial: false});
  for (const immediate of [false, true]) for (const deviceType of ['2in1', 'tablet'])
    cases.push({immediate, deviceType, breakpoint: 'md', storage: {isShowInput: true, isFoldHorizontal: false}});
  for (const immediate of [false, true]) for (const finalize of ['noncallable', 'frozen'])
    cases.push({immediate, finalize, allowThrows: true});
  for (const argumentCount of [0, 1, 2, 3, 4, 6, 200, 201])
    for (const alternate of [false, true]) for (const baseObject of [false, true])
      cases.push({constructOnly: true, argumentCount, alternate, baseObject, allowThrows: argumentCount < 2});
  return cases;
}

export async function verifyCommunityBatch(sources, reference, cases, inventory = false) {
  let count = 0; const counts = [], anchors = []; const views = new Set(), rendered = new Set(), instances = new Set();
  for (const request of cases) {
    const config = {...request};
    if (Object.hasOwn(config, 'failureIndex')) config.failure = [undefined, null, 0, Symbol('raw failure'), {}][config.failureIndex];
    const expected = await exerciseCommunity(reference, config);
    const baseline = !Object.hasOwn(config, 'failAt') && !config.allowThrows;
    if (baseline) assert.ok(expected.modules.every(([, status]) => status === 'evaluated'));
    const throws = expected.trace.filter(([name]) => name.endsWith('.throw'));
    if (baseline) assert.equal(throws.length, 0, JSON.stringify([...new Map(expected.diagnostics.map(entry => [JSON.stringify(entry), entry])).values()]));
    assert.equal(Object.keys(sources).length, 45);
    if (baseline) for (const key of new Set(expected.instances)) assert.ok(key, 'unidentified actual view');
    counts.push(expected.events);
    if (inventory) anchors.push(communityAnchors(expected.sites));
    for (const key of expected.views) views.add(key);
    for (const key of expected.instances) instances.add(key);
    if (!config.constructOnly) for (const key of expected.rendered) rendered.add(key);
    global.gc?.();
    const actual = await exerciseCommunity(sources, config);
    const diagnostics = [actual.diagnostics, expected.diagnostics];
    delete actual.diagnostics; delete expected.diagnostics;
    if (Object.hasOwn(config, 'failAt')) {assert.equal(expected.failed, true); assert.equal(actual.failed, true);}
    if (expected.retainedFault !== undefined) {
      assert.equal(expected.replayedFault, true, 'reference lost retained callback');
      assert.equal(actual.replayedFault, true, 'generated source lost retained callback');
    }
    if (!isDeepStrictEqual(actual, expected)) {
      const differences = [];
      for (const key of Object.keys(expected)) if (!isDeepStrictEqual(actual[key], expected[key])) {
        if (Array.isArray(expected[key]) && Array.isArray(actual[key])) {
          let first = expected[key].findIndex((value, i) => !isDeepStrictEqual(value, actual[key][i]));
          if (first < 0) first = expected[key].length;
          differences.push({key, first, lengths: [actual[key].length, expected[key].length],
            actual: actual[key].slice(Math.max(0, first - 1), first + 2),
            expected: expected[key].slice(Math.max(0, first - 1), first + 2)});
        } else differences.push({key, actual: actual[key], expected: expected[key]});
      }
      assert.fail('community graph mismatch ' + JSON.stringify(request) + '\n' +
        inspect({differences, diagnostics}, {depth: 6, maxArrayLength: 8, maxStringLength: 180}));
    }
    ++count;
  }
  return {cases: count, counts, anchors, rendered: [...rendered], views: [...views], instances: [...instances], generatedModules: Object.keys(sources).length};
}

// Directed semantic boundary probes, not every event or every UI method. They
// cover module publication, construction, value reads/writes, retained work,
// branch/iteration, child updates and cleanup in both callback schedules.
function communityAnchors(sites) {
  const entries = [
    ['route'], ['super'], ['host.get', 'createStorageLink'], ['host.call', 'createStorageLink'],
    ['observed.new'], ['retained', 'observe'], ['retained', 'content'], ['retained', 'list.item'],
    ['host.call', 'forEachUpdateFunction'], ['host.call', 'ifElseBranchUpdateFunction'],
    ['storage.call', 'setOrCreate'], ['host.call', 'updateStateVarsOfChildByElmtId'],
    ['focus.call', 'requestFocus'], ['subscriber0.call', 'Get'],
    ['host.call', 'aboutToBeDeletedInternal'],
  ];
  const anchors = entries.map(([name, key]) => {
    const site = sites.find(([, event, argument]) => event === name && (key === undefined || argument === key));
    assert.ok(site, 'unexecuted boundary ' + name + '.' + key); return site[0];
  });
  const purge = sites.find(([, name, key]) => name.endsWith('.call') && key === 'purgeDependencyOnElmtId');
  assert.ok(purge); anchors.push(purge[0]);
  return anchors;
}
