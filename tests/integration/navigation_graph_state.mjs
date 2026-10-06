import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
import {isDeepStrictEqual} from 'node:util';

const indexName = 'default/src/main/ets/pages/Index';
const pageNames = ['Shopping', 'ShoppingAvoid', 'MiniGame', 'ShoppingFullScreen'];
const libraryNames = ['Home', 'Content', 'Footer', 'Header'];
const classFields = new Map([
  ['Shopping', []], ['ShoppingAvoid', ['__windowUtil']],
  ['MiniGame', ['__windowUtil', '__mainWindowInfo']], ['ShoppingFullScreen', ['__windowUtil']],
  ['Content', ['__mainWindowInfo']], ['Header', []], ['Footer', []],
  ['shopping/src/main/ets/view/Home#Home', ['__activeTabIndex', '__windowUtil', '__mainWindowInfo', 'tabBarList']],
  ['shopping/src/main/ets/view/HomeContent#HomeContent', ['__mainWindowInfo', 'scroller']],
  ['shopping/src/main/ets/view/Header#Header', ['__navPageInfos', '__mainWindowInfo']],
  ['shopping/src/main/ets/view/Categories#Categories', ['__mainWindowInfo', '__isNextCategory', 'categoryList']],
  ['shopping/src/main/ets/view/FlashSale#FlashSale', ['__mainWindowInfo', 'sectionViewModel']],
  ['shopping/src/main/ets/view/Recommend#Recommend', ['__mainWindowInfo', 'recommendProducts']],
  ['shopping/src/main/ets/view/Selection#Selection', ['__mainWindowInfo', 'sectionViewModel']],
  ['shopping/src/main/ets/view/Welfare#Welfare', ['__mainWindowInfo', 'iconInfoViewModel']],
  ['shopping/src/main/ets/view/CommonView#HomeButton', ['buttonText']],
  ['shopping/src/main/ets/view/CommonView#TitleView', ['titleText']],
  ['shopping/src/main/ets/view/CommonView#ProductTitle', ['__isCenter', 'title']],
  ['shopping/src/main/ets/view/CommonView#ProductInfo', ['info']],
  ['shopping/src/main/ets/view/CommonView#ProductPrice', ['price', 'originalPrice']],
  ['shopping/src/main/ets/view/CommonView#ProductFeature', ['feature']],
  ['shopping/src/main/ets/view/CommonView#ProductComments', ['comments']],
]);

// The original ten-module graph controls four leaf classes. The connected
// minigame variant adds the real barrel and all three leaf records; the complete
// variant also links every shopping record. Constructors, methods, field cells
// and resource closures stay intact; only platform services stay controlled in
// the complete variant.
async function exercise(sources, config = {}) {
  const trace = [], sites = [], owners = [], cells = [], leaves = [], pending = [], lifecycle = [], builders = [], clicks = [];
  let itemIdentity = 0;
  const tags = new WeakMap(), modules = new Map(), imports = new Map();
  const connected = Object.hasOwn(sources, 'minigame/Index');
  const allViews = Object.hasOwn(sources, 'shopping/Index');
  const children = new Map(), childCallbacks = [], childTypes = new Map(), viewTypes = new Map(), provided = new Map();
  const tabBuilders = [], changes = [], scrollIndices = [], lazyLists = new WeakSet();
  const failure = Object.hasOwn(config, 'failure') ? config.failure : {};
  let context, factory, nextElement = 0, events = 0, failed = false, retainedFault;
  const replayed = [], pageClasses = new Map();
  const tag = (value, name) => {tags.set(value, name); return value;};
  const token = tag({get then() {assert.fail('void host result was assimilated');}}, 'void');
  const normalize = value => {
    if (value === null || typeof value !== 'object' && typeof value !== 'function') return value;
    if (tags.has(value)) return tags.get(value);
    if (typeof value === 'function') return ['function', value.name, value.length, Object.hasOwn(value, 'prototype')];
    return Reflect.ownKeys(value).map(key => {
      const d = Object.getOwnPropertyDescriptor(value, key); assert.ok('value' in d, `unobserved accessor ${String(key)}`);
      return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
    });
  };
  const outcome = error => {
    if (Object.is(error, failure)) return 'original-throw';
    if (error instanceof assert.AssertionError) throw error;
    assert.ok(['TypeError', 'ReferenceError'].includes(error?.name), String(error)); return error.name;
  };
  const call = (label, callback, receiver, args = []) => {
    try {assert.equal(Reflect.apply(callback, receiver, args), undefined); trace.push([`${label}.return`]); return true;}
    catch (error) {trace.push([`${label}.throw`, outcome(error)]); return false;}
  };
  let components, subscribers, windowSurfaces, leafTypes;
  const event = (name, ...args) => {
    ++events; sites.push([events, name, ...args.map(normalize)]); trace.push([name, ...args.map(normalize)]);
    if (events === config.swapAt) {
      for (const [name, value] of components[1]) context[name] = value;
      context.SubscriberManager = subscribers[1];
      for (const cell of cells) if (cell.name === 'windowUtil') cell.value = windowSurfaces[1];
      context.ObservedPropertyObjectPU = observedTypes[1];
      context.ObservedPropertySimplePU = observedTypes[1];
      context.SynchedPropertyNesedObjectPU = syncedTypes[1];
      for (const module of imports.values()) for (const name of libraryNames)
        if (Object.hasOwn(module.namespace, name)) module.setExport(name, leafTypes[1].get(name));
      trace.push(['live-swap']);
    }
    if (events === config.failAt) {failed = true; throw failure;}
  };
  function surface(name, members) {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); assert.ok(Object.hasOwn(members, key), `${name}.${String(key)}`);
      event(`${name}.get`, key); const value = members[key];
      if (typeof value !== 'function') return value;
      const callback = function(...args) {assert.equal(this, object); event(`${name}.call`, key, ...args); return value(...args);};
      for (const key of ['call', 'apply', 'bind']) Object.defineProperty(callback, key, {get() {assert.fail('ambient invocation adapter');}});
      return callback;
    }});
    return tag(object, name);
  }
  const retain = (kind, callback, receiver, args) => {
    const entry = {kind, callback, receiver, args, id: ++nextElement}; pending.push(entry);
    if (events + 1 === config.failAt) retainedFault = entry.id;
    event('retained', kind, entry.id, callback);
    if (config.immediate) {
      pending.splice(pending.indexOf(entry), 1);
      assert.equal(Reflect.apply(callback, receiver, args), undefined);
    }
  };
  const cell = (owner, name, value) => {
    const state = {owner, name, value, purged: [], deleted: false, reads: 0};
    const object = surface(`cell${cells.length}:${name}`, {
      get() {++state.reads; return config.changing && name === 'windowUtil' && state.reads % 2 === 0 ? null : state.value;},
      set(value) {state.value = value; return token;},
      purgeDependencyOnElmtId(id) {state.purged.push(id); return token;},
      aboutToBeDeleted() {state.deleted = true; return token;},
    });
    cells.push(state); return object;
  };
  function ViewPU(...args) {
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    tag(owner, `${new.target.name}:${owners.length}`); owners.push(owner);
    if (connected && owners.includes(args[0])) children.set(args[2], owner);
    event('super', new.target.name, ...args);
    return config.baseObject ? owner : undefined;
  }
  const methods = {
    addProvidedVar(name, value, flag) {
      assert.equal(name, 'navPageInfos'); assert.equal(value, this.__navPageInfos); assert.equal(flag, false);
      provided.set(name, value); return token;
    },
    initializeConsume(name, alias, initial) {
      assert.equal(name, 'navPageInfos'); assert.equal(alias, name);
      return cell(this, name, provided.has(name) ? provided.get(name).get() : initial);
    },
    createStorageLink(name, initial, alias) {
      assert.equal(name, 'windowUtil'); assert.equal(initial, undefined); assert.equal(alias, name);
      return cell(this, name, Object.hasOwn(config, 'window') ? config.window : windowSurfaces[0]);
    },
    observeComponentCreation2(callback, component) {
      assert.equal(callback.length, 2); assert.equal(Object.hasOwn(callback, 'prototype'), false);
      const args = [nextElement + 1, config.initial ?? true];
      if (connected && (allViews ? Object.hasOwn(component, 'name') : libraryNames.includes(Object.getOwnPropertyDescriptor(component, 'name')?.value))) childCallbacks.push({callback, args});
      retain('observe', callback, {foreign: true}, args); return token;
    },
    ifElseBranchUpdateFunction(branch, callback) {event('branch', this, branch); assert.equal(Reflect.apply(callback, {foreign: true}, []), undefined); return token;},
    forEachUpdateFunction(id, values, generator, key, indexed, keyIndexed) {
      assert.equal(generator.name, 'forEachItemGenFunction'); assert.equal(generator.length, indexed ? 2 : 1);
      assert.equal(Object.hasOwn(generator, 'prototype'), false);
      assert.equal(typeof indexed, 'boolean'); assert.equal(typeof keyIndexed, 'boolean');
      event('foreach', this, id, values, indexed, keyIndexed);
      if (connected) for (const value of values) if (value !== null && typeof value === 'object' && !tags.has(value))
        tag(value, `item${itemIdentity++}`);
      for (let i = 0; i < values.length; ++i) {
        event('key', Reflect.apply(key, {foreign: true}, keyIndexed ? [values[i], i] : [values[i]]));
        assert.equal(Reflect.apply(generator, {foreign: true}, indexed ? [values[i], i] : [values[i]]), undefined);
      }
      return token;
    },
    updateStateVarsOfChildByElmtId(id, params) {
      event('update-child', this, id, params);
      const child = children.get(id);
      if (child) assert.equal(Reflect.apply(child.updateStateVars, child, [params]), undefined);
      return token;
    },
    id__() {return owners.indexOf(this) + 100;},
    aboutToBeDeletedInternal() {return token;}, updateDirtyElements() {return token;},
  };
  for (const [name, body] of Object.entries(methods)) Object.defineProperty(ViewPU.prototype, name, {get() {
    const owner = this; assert.ok(owners.includes(owner)); event('host.get', name, owner);
    return function(...args) {assert.equal(this, owner); event('host.call', name, owner, ...args); return Reflect.apply(body, owner, args);};
  }});
  const finalize = function() {assert.ok(owners.includes(this)); event('finalize', this); return token;};
  if (config.finalize === 'own') ViewPU.prototype.finalizeConstruction = finalize;
  if (config.finalize === 'inherited') Object.setPrototypeOf(ViewPU.prototype, {finalizeConstruction: finalize});
  if (config.finalize === 'noncallable') ViewPU.prototype.finalizeConstruction = 7;
  if (config.finalize === 'frozen') Object.preventExtensions(ViewPU.prototype);
  tag(ViewPU.prototype, 'ViewPU.prototype');
  const renderPage = owner => {
    assert.ok(owners.includes(owner));
    call('page.render', owner.initialRender, owner);
    const state = cells.find(value => value.owner === owner && value.name === 'mainWindowInfo');
    const params = state && connected ? {mainWindowInfo: state.value} : new Proxy({}, {get() {assert.fail('empty update read params');}});
    call('page.update', owner.updateStateVars, owner, [params]);
    call('page.purge', owner.purgeVariableDependenciesOnElmtId, owner, [17]);
    call('page.rerender', owner.rerender, owner); call('page.delete', owner.aboutToBeDeleted, owner);
  };
  Object.defineProperty(ViewPU, 'create', {get() {
    event('view.create.get'); return function(value) {
      assert.equal(this, ViewPU); assert.ok(owners.includes(value) || leaves.includes(value)); event('view.create', value);
      if (owners.includes(value)) renderPage(value); return token;
    };
  }});
  function NavPathStack() {return surface(`path${owners.length}`, {pushPath(value) {assert.deepEqual(Reflect.ownKeys(value), ['name']); return token;}, pop() {return token;}});}
  const observedTypes = [0, 1].map(version => function Observed(value, owner, name) {
    assert.ok(owners.includes(owner)); event('observed.new', version, owner, name, value); return cell(owner, name, value);
  });
  const syncedTypes = [0, 1].map(version => function Synched(value, owner, name) {
    assert.ok(owners.includes(owner)); event('synced.new', version, owner, name, value); return cell(owner, name, value);
  });
  windowSurfaces = [0, 1].map(version => {
    const system = surface(`system${version}`, {topRect: surface(`top${version}`, {height: 24 + version})});
    const navigation = surface(`navigation${version}`, {bottomRect: surface(`bottom${version}`, {height: 12 + version})});
    const info = surface(`info${version}`, {avoidSystem: system, avoidNavigationIndicator: navigation,
      avoidAreaPadding: tag({top: 7 + version}, `padding${version}`), widthBp: config.width ?? (version ? 'xl' : 'sm'),
      isImmersive: config.immersive ?? version,
      titleButtonRect: Object.hasOwn(config, 'titleRect') ? config.titleRect : surface(`title${version}`, {width: 30 + version})});
    return surface(`window${version}`, {mainWindowInfo: info, setImmersiveType: () => token,
      startWindowStatusListener: () => token, release: () => token, setSystemBarEnabled: () => token});
  });
  const keys = ['create', 'pop', 'width', 'height', 'padding', 'fontWeight', 'fontSize', 'fontColor', 'margin',
    'backgroundColor', 'background', 'borderRadius', 'color', 'strokeWidth', 'onClick', 'layoutWeight',
    'hideToolBar', 'hideTitleBar', 'mode', 'navDestination', 'ignoreLayoutSafeArea', 'backgroundImage',
    'backgroundImageSize', 'onShown', 'onHidden', 'scrollBar', 'columnsTemplate', 'aspectRatio',
    'borderColor', 'borderWidth', 'alignItems', 'justifyContent', 'linearGradient', 'createWithLabel',
    'border', 'decoration', 'lineHeight', 'maxLines', 'opacity', 'textAlign', 'textOverflow', 'fillColor',
    'objectFit', 'alignContent', 'alignSelf', 'displayCount', 'indicator', 'itemSpace', 'lanes', 'listDirection',
    'onScrollIndex', 'lineCap', 'alignListItem', 'enableKeyboardOnFocus', 'placeholderColor', 'placeholderFont',
    'barHeight', 'barWidth', 'barMode', 'onChange', 'vertical', 'tabBar'];
  components = [0, 1].map(version => new Map(['Navigation', 'NavDestination', 'Column', 'Text', 'ForEach', 'If', 'Divider', 'Row', 'Span', 'Image', 'Grid', 'GridItem', 'Blank', 'Scroll', 'Button', 'GridRow', 'GridCol', 'Stack', 'Swiper', 'List', 'ListItem', 'TextInput', 'Tabs', 'TabContent', '__Common__'].map(name =>
    [name, surface(`${name}${version}`, Object.fromEntries(keys.map(key => [key, (...args) => {
      if (name === 'GridItem' && key === 'create') assert.equal(Reflect.apply(args[0], {foreign: true}, []), undefined);
      if (name === 'NavDestination' && key === 'create') retain('content', args[0], {foreign: true}, []);
      if (name === 'TabContent' && key === 'create') retain('tab-content', args[0], {foreign: true}, []);
      // The host schedules the original lazy renderer once. Its own create
      // call retains that identity without recursively scheduling another render.
      if (name === 'ListItem' && key === 'create' && !lazyLists.has(args[0])) {
        lazyLists.add(args[0]); retain('list-item', args[0], {foreign: true}, [nextElement + 1, config.initial ?? true]);
      }
      if (key === 'tabBar') tabBuilders.push(args[0].builder);
      if (key === 'onChange') changes.push(args[0]);
      if (key === 'onScrollIndex') scrollIndices.push(args[0]);
      if (key === 'navDestination') builders.push(args[0].builder);
      if (key === 'onClick') clicks.push(args[0]);
      if (key === 'onShown' || key === 'onHidden') lifecycle.push(args[0]);
      return token;
    }])))])));
  subscribers = [0, 1].map(version => surface(`SubscriberManager${version}`, {
    Get: () => surface(`subscriber${version}`, {delete: () => token}),
  }));
  leafTypes = [0, 1].map(version => new Map(libraryNames.map(name => {
    const Type = function(parent, params, storage, id, callback, metadata) {
      assert.ok(owners.includes(parent)); assert.equal(storage, undefined);
      assert.equal(Reflect.apply(callback, {foreign: true}, []), undefined);
      tag(this, `${name}${version}:${leaves.length}`); leaves.push(this); event('leaf.new', name, version, parent, params, id, metadata);
    };
    Object.defineProperty(Type, 'name', {value: name});
    Object.defineProperty(Type.prototype, 'paramsGenerator_', {set(callback) {
      assert.equal(callback.name, 'paramsLambda'); assert.equal(callback.length, 0);
      const a = Reflect.apply(callback, {foreign: true}, []), b = Reflect.apply(callback, null, []);
      assert.notEqual(a, b); event('leaf.params', this, a, b);
    }}); return [name, Type];
  })));
  context = vm.createContext({ViewPU, NavPathStack, ObservedPropertyObjectPU: observedTypes[0], ObservedPropertySimplePU: observedTypes[0],
    SynchedPropertyNesedObjectPU: syncedTypes[0],
    WidthBreakpoint: surface('breakpoint', {WIDTH_SM: 'sm', WIDTH_MD: 'md', WIDTH_LG: 'lg', WIDTH_XL: 'xl', WIDTH_XS: 'xs'}),
    BarState: surface('bar', {Off: 'off'}), VerticalAlign: surface('vertical', {Top: 'top', Bottom: 'bottom', Center: 'center'}),
    FlexAlign: surface('flex', {Start: 'start', Center: 'center', SpaceBetween: 'space-between'}),
    GradientDirection: surface('gradient', {Right: 'right'}), TextAlign: surface('text-align', {Center: 'center'}),
    TextOverflow: surface('overflow', {Ellipsis: 'ellipsis'}), TextDecorationType: surface('decoration', {LineThrough: 'line-through'}),
    BorderStyle: surface('border', {Solid: 'solid'}), HorizontalAlign: surface('horizontal', {Start: 'start'}),
    ItemAlign: surface('item-align', {Center: 'center', End: 'end'}), Alignment: surface('align', {Start: 'start', TopEnd: 'top-end'}),
    ImageFit: surface('image-fit', {Contain: 'contain'}), Axis: surface('axis', {Horizontal: 'horizontal'}),
    LineCapStyle: surface('line-cap', {Round: 'round'}), ListItemAlign: surface('list-align', {Center: 'center'}),
    BarPosition: surface('bar-position', {Start: 'start', End: 'end'}), BarMode: surface('bar-mode', {Fixed: 'fixed'}),
    ViewStackProcessor: surface('view-stack', {StartGetAccessRecordingFor: () => token, StopGetAccessRecording: () => token}),
    Scroller: function Scroller() {tag(this, 'scroller'); event('scroller.new');},
    ...Object.fromEntries(components[0]), SubscriberManager: subscribers[0],
    NavigationMode: surface('mode', {Stack: 'stack'}), FontWeight: surface('weight', {Bold: 'bold', Medium: 'medium', Normal: 'normal'}),
    Color: surface('color', {White: 'white', Black: 'black'}), LayoutPolicy: surface('layout', {matchParent: 'parent'}),
    ImageSize: surface('imageSize', {FILL: 'fill'}), Observed: Type => Type,
    registerNamedRoute(callback, name, metadata) {assert.equal(this, undefined); factory = callback; event('route', name, metadata); return token;},
  });
  for (const [name, source] of Object.entries(sources)) modules.set(name, new vm.SourceTextModule(source, {context, identifier: name}));
  const root = modules.get(indexName);
  await root.link(specifier => {
    const name = specifier.startsWith('@normalized:N&&&') ? specifier.slice('@normalized:N&&&'.length).split('&')[0] : specifier;
    if (modules.has(name)) return modules.get(name);
    if (imports.has(name)) return imports.get(name);
    assert.ok(['shopping/Index', 'minigame/Index'].includes(name) || name.startsWith('@ohos:'), specifier);
    const names = name === 'shopping/Index' ? ['Home'] : name === 'minigame/Index' ? ['Content', 'Footer', 'Header'] : ['default'];
    const module = new vm.SyntheticModule(names, function() {
      for (const key of names) this.setExport(key, key === 'default' ? tag({}, name) : leafTypes[0].get(key));
    }, {context}); imports.set(name, module); return module;
  });
  let error, moduleFailed = false;
  try {await root.evaluate(); trace.push(['module.return']);}
  catch (value) {error = value; moduleFailed = true; trace.push(['module.throw', outcome(value)]);}
  const before = events;
  if (moduleFailed) await assert.rejects(root.evaluate(), value => Object.is(value, error)); else await root.evaluate();
  assert.equal(events, before);
  for (const [name, module] of modules) if (module.status === 'evaluated') for (const page of pageNames)
    if (Object.hasOwn(module.namespace, page)) pageClasses.set(page, module.namespace[page]);
  for (const module of modules.values()) if (module.status === 'evaluated') for (const name of ['Content', 'Header', 'Footer'])
    if (Object.hasOwn(module.namespace, name)) childTypes.set(name, module.namespace[name]);
  if (allViews) for (const [name, module] of modules) if (module.status === 'evaluated')
    for (const [key, Type] of Object.entries(module.namespace)) if (typeof Type === 'function' && Object.getPrototypeOf(Type) === ViewPU)
      viewTypes.set(`${name}#${key}`, Type);
  if (factory && !config.directOnly) {
    try {
      const owner = Reflect.apply(factory, {foreign: true}, []); assert.equal(owner, owners[0]);
      call('index.render', owner.initialRender, owner);
      for (const name of pageNames) call('index.navmap', owner.NavPageMap, owner, [name]);
      call('index.purge', owner.purgeVariableDependenciesOnElmtId, owner, [17]);
      call('index.rerender', owner.rerender, owner); call('index.delete', owner.aboutToBeDeleted, owner);
    } catch (value) {trace.push(['factory.throw', outcome(value)]);}
  }
  const drain = () => {
    while (pending.length) {
      const batch = pending.splice(0); if (config.reverse) batch.reverse();
      for (const entry of batch) replayed.push([entry.id, call(`retained.${entry.kind}`, entry.callback, entry.receiver, entry.args)]);
    }
  };
  drain();
  for (const click of clicks) call('click', click, {foreign: true});
  for (const builder of builders) for (const name of pageNames) call('builder', builder, {foreign: true}, [name]);
  drain();
  if (allViews) {
    for (const callback of changes) for (const index of [0, 1, 3]) call('tab.change', callback, {foreign: true}, [index]);
    for (const callback of scrollIndices) for (const args of [[1, 9], [0, 9], [1, 8]]) call('list.scroll', callback, {foreign: true}, args);
    for (const callback of tabBuilders) call('tab.builder', callback, {foreign: true});
    drain();
  }
  if (connected) {
    // Replay the actual parent's update branch and its late parameter closures.
    // Keep child element identity and pass the new parent value into real cells.
    for (const state of cells) if (state.name === 'windowUtil') state.value = windowSurfaces[1];
    for (const {callback, args} of childCallbacks) call('child.update', callback, {foreign: true}, [args[0], false]);
    for (const owner of owners) if ((allViews || childTypes.has(owner.constructor.name)) && Object.hasOwn(owner, 'paramsGenerator_')) {
      try {
        const a = Reflect.apply(owner.paramsGenerator_, {foreign: true}, []);
        const b = Reflect.apply(owner.paramsGenerator_, null, []);
        if (a !== undefined) assert.notEqual(a, b);
        event('child.params', owner, a, b);
        call('child.params.update', owner.updateStateVars, owner, [a]);
      } catch (error) {trace.push(['child.params.throw', outcome(error)]);}
    }
  }
  for (const callback of lifecycle) call('page.lifecycle', callback, {foreign: true});
  let assignment;
  if (config.direct) {
    const Type = viewTypes.get(config.direct) ?? pageClasses.get(config.direct) ??
      (allViews ? viewTypes.get(`minigame/src/main/ets/view/${config.direct}#${config.direct}`) : childTypes.get(config.direct)); assert.ok(Type);
    assert.equal(Type.name, config.direct.split('#').at(-1)); assert.equal(Type.length, 3);
    assert.equal(Object.getPrototypeOf(Type), ViewPU); assert.equal(Object.getPrototypeOf(Type.prototype), ViewPU.prototype);
    assert.throws(() => Reflect.apply(Type, {}, []), {name: 'TypeError'});
    let writes = 0, receiver, stored;
    if (config.assignment) Object.defineProperty(Type.prototype, config.assignment, {configurable: true, set(value) {
      ++writes; receiver = this; stored = value; event('field.assignment', this, config.assignment, value);
      if (config.setterThrows) throw failure;
      Object.defineProperty(this, config.assignment, {value, writable: true, enumerable: true, configurable: true});
    }});
    const Target = function Alternative() {}, prototype = Object.create(Type.prototype); Target.prototype = prototype;
    const info = surface('provided-info', {avoidAreaPadding: tag({top: 19}, 'provided-padding')});
    const params = {mainWindowInfo: info}, lambda = () => ({});
    const supplied = ['parent', params, 'storage', config.elmtId, lambda, 'extra'];
    const args = Array.from({length: config.argumentCount ?? 6}, (_, i) => i < supplied.length ? supplied[i] : tag({}, `tail${i}`));
    const before = owners.length;
    try {
      const value = Reflect.construct(Type, args, config.alternate ? Target : Type);
      assert.equal(Object.getPrototypeOf(value), config.alternate ? prototype : Type.prototype);
      trace.push(['direct.return', value.constructor.name]);
    } catch (error) {trace.push(['direct.throw', outcome(error)]);}
    if (config.assignment) {
      assert.equal(writes, 1); assert.equal(receiver, owners[before]);
      assert.equal(Object.getPrototypeOf(receiver), config.alternate ? prototype : Type.prototype);
      const ordered = ['paramsGenerator_', ...classFields.get(config.direct)];
      assert.deepEqual(Reflect.ownKeys(receiver), config.setterThrows ? ordered.slice(0, ordered.indexOf(config.assignment)) : ordered);
      assignment = {writes, receiver: tags.get(receiver), value: normalize(stored)};
      delete Type.prototype[config.assignment];
    }
  }
  const state = owners.map(owner => Reflect.ownKeys(owner).map(key => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, key); assert.ok('value' in descriptor);
    return [key, normalize(descriptor.value), descriptor.writable, descriptor.enumerable, descriptor.configurable];
  }));
  const metadata = (allViews ? [...viewTypes] : [...pageClasses, ...childTypes]).map(([name, Type]) => [name, Type.name, Type.length,
    Reflect.ownKeys(Type.prototype).map(key => {
      const d = Object.getOwnPropertyDescriptor(Type.prototype, key);
      return [key, key === 'constructor' ? d.value === Type : normalize(d.value), normalize(d.get), normalize(d.set), d.writable, d.enumerable, d.configurable];
    })]);
  const viewNames = new Map([...viewTypes].map(([name, Type]) => [Type, name]));
  const instances = owners.map(owner => viewNames.get(owner.constructor));
  return {trace, sites, state, metadata, assignment, cells: cells.map(value => [tags.get(value.owner), value.name, normalize(value.value), value.purged, value.deleted]),
    events, failed, retainedFault, replayed, pages: owners.map(owner => owner.constructor.name),
    views: [...viewTypes.keys()], instances,
    modules: [...modules].map(([name, module]) => [name, module.status])};
}

export async function verifyNavigationBatch(sources, reference, request) {
  const base = {finalize: 'own'};
  const connected = request.connected ?? false;
  const allViews = request.allViews ?? false;
  const directClasses = allViews ? [...classFields.keys()] : connected ? [...classFields.keys()].filter(name => !name.startsWith('shopping/')) : pageNames;
  const expected = await exercise(reference, base);
  const immediateExpected = await exercise(reference, {...base, immediate: true});
  const counts = {deferred: expected.events, immediate: immediateExpected.events};
  if (connected) for (const value of [expected, immediateExpected]) {
    assert.ok(value.modules.every(([, status]) => status === 'evaluated'));
    assert.ok(value.trace.every(([name]) => !name.endsWith('.throw')), JSON.stringify(value.trace.filter(([name]) => name.endsWith('.throw'))));
    for (const name of ['Content', 'Header', 'Footer']) assert.ok(value.pages.includes(name), `missing actual ${name}`);
    const images = value.trace.filter(([name, key, item]) => name === 'Image0.call' && key === 'create' && typeof item === 'string' && item.startsWith('item'));
    assert.equal(images.length, value.pages.filter(name => name === 'Content').length * 60);
    for (const cell of value.cells) if (cell[1] === 'mainWindowInfo' && cell[0].startsWith('Content:')) assert.equal(cell[2], 'info1');
  }
  if (allViews) {
    assert.equal(Object.keys(sources).length, 33);
    for (const value of [expected, immediateExpected]) for (const key of classFields.keys()) if (key.startsWith('shopping/')) {
      assert.ok(value.views.includes(key), `missing view export ${key}`);
      assert.ok(value.instances.includes(key), `unexecuted ${key}`);
    }
  }
  // The original graph and standalone Content already cover every event fault.
  // This integration matrix probes each distinct observable host boundary here;
  // repeated resource/index positions are still compared in full lifecycle traces.
  const boundaries = value => {
    const seen = new Set();
    return value.sites.filter(([, name, argument]) => {
      const key = JSON.stringify([name, typeof argument === 'string' ? argument : null]);
      if (seen.has(key)) return false; seen.add(key); return true;
    }).map(([ordinal]) => ordinal);
  };
  const ordinals = {deferred: connected ? boundaries(expected) : Array.from({length: counts.deferred}, (_, i) => i + 1),
    immediate: connected ? boundaries(immediateExpected) : Array.from({length: counts.immediate}, (_, i) => i + 1)};
  const inventory = {lifecycle: 0, faults: 0, assignments: 0, mutations: 0}, result = {cases: 0, faults: 0, swaps: 0, assignments: 0, mutations: 0};
  const selected = phase => {const i = inventory[phase]++; return request.phase === phase && i >= request.first && i < request.first + request.count;};
  const compare = async config => {
    const expected = await exercise(reference, config), actual = await exercise(sources, config);
    // Compare the entire result, but keep a mismatch from formatting tens of
    // thousands of trace entries inside the bounded worker. The case range
    // identifies an exact independent replay, including non-JSON throw values.
    assert.ok(isDeepStrictEqual(actual, expected),
      `source/reference mismatch in ${request.phase} batch ${request.first}, case ${inventory[request.phase] - 1}`);
    return actual;
  };
  for (const immediate of [false, true]) for (const reverse of [false, true]) for (const initial of [false, true])
    for (const baseObject of [false, true]) for (const finalize of ['own', 'absent', 'inherited', 'noncallable', 'frozen']) if (selected('lifecycle')) {
      await compare({immediate, reverse, initial, baseObject, finalize}); ++result.cases;
    }
  for (const window of [undefined, null, false, 0, -0, NaN, '', 1n, Symbol('window')])
    for (const immediate of [false, true]) if (selected('lifecycle')) {await compare({...base, window, immediate}); ++result.cases;}
  for (const changing of [false, true]) for (const immediate of [false, true]) if (selected('lifecycle')) {
    await compare({...base, changing, immediate}); ++result.cases;
  }
  if (allViews) for (const width of ['xs', 'sm', 'md', 'lg', 'xl']) for (const immersive of [0, 1])
    for (const immediate of [false, true]) if (selected('lifecycle')) {
      await compare({...base, width, immersive, immediate}); ++result.cases;
    }
  if (allViews) for (const titleRect of [undefined, null, {width: 0}, {width: 7}])
    for (const immediate of [false, true]) if (selected('lifecycle')) {
      await compare({...base, titleRect, immediate}); ++result.cases;
    }
  for (const direct of directClasses) for (const argumentCount of [0, 1, 2, 3, 4, 6, 200, 201])
    for (const alternate of [false, true]) if (selected('lifecycle')) {
      await compare({...base, direct, argumentCount, alternate, directOnly: allViews}); ++result.cases;
    }
  for (const immediate of [false, true]) for (const ordinal of ordinals[immediate ? 'immediate' : 'deferred']) {
    for (const failure of [undefined, null, 0, Symbol('failure'), {}]) if (selected('faults')) {
      const value = await compare({...base, immediate, failAt: ordinal, failure}); assert.equal(value.failed, true);
      if (value.retainedFault !== undefined) assert.ok(value.replayed.some(([id, completed]) => id === value.retainedFault && completed),
        'the exact callback retained before registration threw was not successfully replayed');
      ++result.faults;
    }
    if (selected('faults')) {await compare({...base, immediate, swapAt: ordinal}); ++result.swaps;}
  }
  for (const direct of directClasses) for (const assignment of ['paramsGenerator_', ...classFields.get(direct)]) for (const alternate of [false, true])
    for (const setterThrows of [false, true]) for (const failure of setterThrows ? [undefined, null, 0, Symbol('setter'), {}] : [undefined])
      if (selected('assignments')) {
        await compare({...base, direct, assignment, alternate, setterThrows, failure, directOnly: allViews}); ++result.assignments;
      }
  const recordFor = page => Object.keys(sources).find(name => name === `minigame/src/main/ets/view/${page}` || name.endsWith(`/${page}`) && !name.startsWith('shopping/'));
  const constructorRead = expected.sites.findIndex(([, name, argument]) => name === 'super' && argument === 'MiniGame');
  assert.ok(constructorRead >= 0);
  const capturedClassSite = expected.sites.slice(constructorRead).find(([, name, key]) => name === 'window0.get' && key === 'mainWindowInfo');
  assert.ok(capturedClassSite);
  const mutants = [
    ['Shopping', 'super(parent, __localStorage, elmtId, extraInfo)', 'super(parent, params, elmtId, extraInfo)'],
    ['Shopping', '($local0 = this);', '($local0 = globalThis);'],
    ['ShoppingAvoid', '?.topRect.height', '?.topRect.height + 1'],
    ['ShoppingAvoid', 'this.__windowUtil.aboutToBeDeleted();', 'this.__windowUtil.get();'],
    ['MiniGame', '?.setSystemBarEnabled(false);', '?.setSystemBarEnabled(true);'],
    ['MiniGame', 'new $value7($phi6, this, "mainWindowInfo")', 'new ObservedPropertyObjectPU($phi6, this, "mainWindowInfo")', {swapAt: capturedClassSite[0]}],
    ['MiniGame', 'this.__mainWindowInfo.set(newValue);', 'this.__windowUtil.set(newValue);', {direct: 'MiniGame'}],
    ['MiniGame', '($value0 = $local0.windowUtil)?.mainWindowInfo', '($value0 = globalThis.windowUtil)?.mainWindowInfo'],
    ['ShoppingFullScreen', '?.setImmersiveType(ImmersiveType.IMMERSIVE);', '?.setImmersiveType(ImmersiveType.NORMAL);'],
    ['ShoppingFullScreen', '?.release();', '?.startWindowStatusListener();'],
    ['ShoppingFullScreen', 'this.updateDirtyElements();', 'return this.updateDirtyElements();'],
    ['ShoppingAvoid', '(this.__windowUtil = this.createStorageLink("windowUtil", (void 0), "windowUtil"));',
      'Object.defineProperty(this, "__windowUtil", {value: this.createStorageLink("windowUtil", (void 0), "windowUtil"), writable: true, enumerable: true, configurable: true});',
      {direct: 'ShoppingAvoid', assignment: '__windowUtil'}],
  ];
  if (connected) mutants.push(
    ['MiniGame', 'new Content($local0,', 'new Footer($local0,'],
    ['MiniGame', 'return ({mainWindowInfo: (($value0 = $local0.windowUtil)?.mainWindowInfo)});',
      'return ({mainWindowInfo: $local0.mainWindowInfo});'],
    ['Content', 'params.mainWindowInfo, this, "mainWindowInfo"', 'params.windowUtil, this, "mainWindowInfo"'],
    ['Content', 'this.__mainWindowInfo.set(params.mainWindowInfo);', 'this.__mainWindowInfo.set(params.windowUtil);'],
    ['Content', '($local0 = this);', '($local0 = globalThis);'],
    ['Content', 'Image.create(image);', 'Image.create({...image});'],
    ['Content', 'JSON.stringify(image) + index', 'JSON.stringify(image) + 0'],
    ['Content', 'getValue($local0.mainWindowInfo.widthBp)', 'getValue($local0.mainWindowInfo.heightBp)'],
    ['Header', 'Row.alignItems(VerticalAlign.Top);', 'Row.alignItems(VerticalAlign.Bottom);'],
    ['Footer', 'this.id__()', 'undefined'],
    ['minigame/Index', 'export { Content as Content } from "@normalized:N&&&minigame/src/main/ets/view/Content&1.0.0";',
      'export { Footer as Content } from "@normalized:N&&&minigame/src/main/ets/view/Footer&1.0.0";'],
  );
  if (allViews) mutants.push(
    ['shopping/src/main/ets/view/Home', 'this.__activeTabIndex.set(newValue);', 'this.__windowUtil.set(newValue);'],
    ['shopping/src/main/ets/view/Home', 'this.activeTabIndex === index', 'this.activeTabIndex !== index'],
    ['shopping/src/main/ets/view/Home', '($local0 = this);', '($local0 = globalThis);'],
    ['shopping/src/main/ets/view/Home', 'TabBarBuilder.call($local0, tabBar, $local2)', 'TabBarBuilder.call(globalThis, tabBar, $local2)'],
    ['shopping/src/main/ets/view/Home', '$local4.activeIcon : $local4.icon', '$local4.icon : $local4.activeIcon'],
    ['shopping/src/main/ets/view/HomeContent', 'this.__mainWindowInfo.set(params.mainWindowInfo);', 'this.__mainWindowInfo.set(params.windowUtil);'],
    ['shopping/src/main/ets/view/Header', '$local1.navPageInfos.pop();', '$local1.navPageInfos.pushPath({name: "wrong"});'],
    ['shopping/src/main/ets/view/Categories', 'end === HomeConstants.NINE', 'end === HomeConstants.NINE + 1'],
    ['shopping/src/main/ets/view/Categories', 'Image.create(item.getIcon());', 'Image.create(item.getInfo());'],
    ['shopping/src/main/ets/view/CommonView', 'this.__isCenter.set(newValue);', 'this.__isCenter.get();'],
    ['shopping/src/main/ets/viewmodel/HomeViewModel', 'return this.homeModel.getTabBarList();', 'return this.homeModel.getTabBarList().slice(1);'],
  );
  for (const [page, before, after, config = {}] of mutants) if (selected('mutations')) {
    const name = Object.hasOwn(sources, page) ? page : recordFor(page), changed = sources[name].replace(before, after); assert.notEqual(changed, sources[name]);
    new vm.SourceTextModule(changed);
    await assert.rejects(async () => {
      const actual = await exercise({...sources, [name]: changed}, {...base, ...config});
      // A deliberate mismatch across thousands of retained events must not
      // spend the worker budget constructing an enormous assertion diff.
      assert.ok(isDeepStrictEqual(actual, await exercise(reference, {...base, ...config})), `undetected ${page} mutation`);
    }, error => error instanceof assert.AssertionError);
    ++result.mutations;
  }
  return {...result, counts, inventory, boundaries: {deferred: ordinals.deferred.length, immediate: ordinals.immediate.length}};
}

export async function verifyNavigationGraph(sources, reference, {connected = false, allViews = false, shard = {index: 0, count: 1}} = {}) {
  const worker = fileURLToPath(new URL('./navigation_graph_worker.mjs', import.meta.url));
  const run = request => {
    try {
      return JSON.parse(execFileSync(process.execPath, ['--experimental-vm-modules', worker], {
        input: JSON.stringify({sources, reference, request: {...request, connected, allViews}}), encoding: 'utf8', timeout: 60_000,
        maxBuffer: 8 * 1024 * 1024, stdio: 'pipe',
      }));
    } catch (error) {
      error.message = `${request.phase} batch ${request.first} count ${request.count}: ${error.message}`;
      throw error;
    }
  };
  const {counts, inventory, boundaries} = run({phase: 'inventory', first: 0, count: 0});
  const result = {cases: 0, faults: 0, swaps: 0, assignments: 0, mutations: 0};
  const selectedCounts = {};
  // A 64-case worker exceeded the existing sixty-second limit while four
  // graph shards ran together. Bound each job more tightly; retain every case
  // and the original timeout across all variants.
  const batchCaseLimit = 32;
  // Partition the entire independent case inventory, not selected examples.
  // Adjacent integer ranges cover each phase exactly once across all shards;
  // each worker still has the original bounded batch and sixty-second limit.
  for (const [phase, count] of Object.entries(inventory)) {
    const begin = Math.floor(count * shard.index / shard.count);
    const end = Math.floor(count * (shard.index + 1) / shard.count);
    selectedCounts[phase] = end - begin;
    if (allViews) process.stderr.write(`navigation shard ${shard.index + 1}/${shard.count}: ${phase} ${begin}..${end}\n`);
    for (let first = begin; first < end; first += batchCaseLimit) {
      const batchCount = Math.min(batchCaseLimit, end - first);
      const row = run({phase, first, count: batchCount});
      assert.deepEqual(row.counts, counts); assert.deepEqual(row.inventory, inventory); assert.deepEqual(row.boundaries, boundaries);
      for (const key of Object.keys(result)) result[key] += row[key];
      if (allViews) process.stderr.write(`navigation shard ${shard.index + 1}/${shard.count}: ${phase} completed ${first + batchCount - begin}/${end - begin}\n`);
    }
  }
  assert.equal(result.cases, selectedCounts.lifecycle); assert.equal(result.faults + result.swaps, selectedCounts.faults);
  assert.equal(result.assignments, selectedCounts.assignments); assert.equal(result.mutations, selectedCounts.mutations);
  return {...result, counts, boundaries, inventory, shard, generatedModules: Object.keys(sources).length, wholeGraphBehavior: true, realMinigameViews: connected, realShoppingViews: allViews,
    faultScope: connected ? 'distinct-host-boundaries' : 'every-event', platformExecution: false};
}
