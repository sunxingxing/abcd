import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';

const routeSpecifier = '@normalized:N&&&default/src/main/ets/constants/Constants&';
const childNames = ['Shopping', 'ShoppingAvoid', 'MiniGame', 'ShoppingFullScreen'];

// Execute the complete Index + Constants ESM and real Index methods/closures.
// Only imported child views and platform services are controlled. No reference
// method, constructor or fragment is substituted into the generated module.
async function exercise(source, routes, config = {}) {
  const trace = [], owners = [], cells = [], paths = [], callbacks = [], clicks = [], builders = [], children = [];
  const tags = new WeakMap(), failure = Object.hasOwn(config, 'failure') ? config.failure : {};
  const tag = (value, label) => {tags.set(value, label); return value;};
  let context, Type, factory, moduleError, moduleFailed = false, failed = false, events = 0, nextElement = 0;
  let retainedFaultId;
  const replayed = [];
  const token = tag({get then() {assert.fail('void platform result was assimilated');}}, 'void-result');
  const normalize = value => {
    if (value === null || typeof value !== 'object' && typeof value !== 'function') return value;
    if (tags.has(value)) return tags.get(value);
    if (typeof value === 'function') return ['callback', value.name, value.length, Object.hasOwn(value, 'prototype')];
    return Reflect.ownKeys(value).map(key => {
      const d = Object.getOwnPropertyDescriptor(value, key);
      assert.ok('value' in d, `oracle attempted an accessor: ${String(key)}`);
      return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
    });
  };
  const outcome = error => {
    if (Object.is(error, failure)) return 'original-throw';
    if (error instanceof assert.AssertionError) throw error;
    assert.ok(['TypeError', 'ReferenceError'].includes(error?.name), String(error));
    return error.name;
  };
  const call = (label, fn, receiver, args = []) => {
    try {assert.equal(Reflect.apply(fn, receiver, args), undefined); trace.push([`${label}.return`]); return true;}
    catch (error) {trace.push([`${label}.throw`, outcome(error)]); return false;}
  };
  let components, subscribers, childModules;
  const event = (label, ...args) => {
    ++events; trace.push([label, ...args.map(normalize)]);
    if (events === config.swapAt) {
      for (const [name, value] of components[1]) context[name] = value;
      context.SubscriberManager = subscribers[1];
      context.JSON = jsonSurfaces[1];
      context.NavPathStack = pathTypes[1]; context.ObservedPropertyObjectPU = observedTypes[1];
      for (const [name, dependency] of childModules) dependency.setExport(name, childTypes[1].get(name));
      trace.push(['live-swap']);
    }
    if (events === config.failAt) {failed = true; throw failure;}
  };
  function surface(label, methods) {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); assert.ok(Object.hasOwn(methods, key), `${label}.${String(key)}`);
      event(`${label}.get`, key);
      const body = methods[key];
      if (typeof body !== 'function') return body;
      const method = function(...args) {assert.equal(this, object); event(`${label}.call`, key, ...args); return body(...args);};
      for (const name of ['call', 'apply', 'bind']) Object.defineProperty(method, name,
        {get() {assert.fail('ambient call adapter');}});
      return method;
    }});
    return tag(object, label);
  }
  const createCell = (owner, alias, value) => {
    const state = {owner, alias, value, deleted: false, purged: []};
    const object = surface(`cell${cells.length}:${alias}`, {
      get: () => state.value,
      set(value) {state.value = value; return token;},
      purgeDependencyOnElmtId(id) {state.purged.push(id); return token;},
      aboutToBeDeleted() {state.deleted = true; return token;},
    });
    cells.push(state); return object;
  };
  function ViewPU(...args) {
    Type ??= new.target;
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    tag(owner, `owner${owners.length}`); owners.push(owner); event('super', ...args);
    return config.baseObject ? owner : undefined;
  }
  Object.defineProperty(ViewPU, 'create', {get() {
    event('view.create.get');
    return function(child) {assert.equal(this, ViewPU); assert.ok(children.includes(child)); event('view.create', child); return token;};
  }});
  const methods = {
    addProvidedVar(name, value, flag) {
      assert.equal(name, 'navPageInfos'); assert.equal(value, this.__navPageInfos); assert.equal(flag, false); return token;
    },
    createStorageLink(name, value, alias) {
      assert.equal(name, 'windowUtil'); assert.equal(alias, name); assert.equal(value, undefined);
      return createCell(this, alias, value);
    },
    observeComponentCreation2(callback, component) {
      assert.equal(callback.length, 2); assert.equal(Object.hasOwn(callback, 'prototype'), false);
      const entry = {owner: this, callback, component, id: ++nextElement};
      callbacks.push(entry);
      if (events + 1 === config.failAt) retainedFaultId = entry.id;
      event('observe.retained', this, component, entry.id);
      if (config.immediate) assert.equal(Reflect.apply(callback, {foreign: true}, [entry.id, config.initial ?? true]), undefined);
      return token;
    },
    ifElseBranchUpdateFunction(branch, callback) {
      event('branch', this, branch); assert.equal(Reflect.apply(callback, {foreign: true}, []), undefined); return token;
    },
    forEachUpdateFunction(id, values, generator, key, indexed, keyIndexed) {
      assert.equal(generator.name, 'forEachItemGenFunction'); assert.equal(generator.length, indexed ? 2 : 1);
      assert.equal(Object.hasOwn(generator, 'prototype'), false); assert.equal(key.length, 1); assert.equal(keyIndexed, false);
      event('foreach', this, id, values, indexed);
      if (!Array.isArray(values)) return token;
      for (let i = 0; i < values.length; ++i) {
        event('key', Reflect.apply(key, {foreign: true}, [values[i]]));
        assert.equal(Reflect.apply(generator, {foreign: true}, indexed ? [values[i], i] : [values[i]]), undefined);
      }
      return token;
    },
    updateStateVarsOfChildByElmtId(id, params) {assert.deepEqual(Reflect.ownKeys(params), []); return token;},
    id__() {return owners.indexOf(this) + 100;},
    aboutToBeDeletedInternal() {return token;},
    updateDirtyElements() {return token;},
  };
  for (const [name, body] of Object.entries(methods)) Object.defineProperty(ViewPU.prototype, name, {
    configurable: true, get() {
      const owner = this; assert.ok(owners.includes(owner)); event('host.get', name, owner);
      return function(...args) {assert.equal(this, owner); event('host.call', name, owner, ...args); return Reflect.apply(body, owner, args);};
    },
  });
  const finalize = function() {assert.ok(owners.includes(this)); event('finalize', this); return token;};
  if (config.finalize === 'own') ViewPU.prototype.finalizeConstruction = finalize;
  if (config.finalize === 'inherited') Object.setPrototypeOf(ViewPU.prototype, {finalizeConstruction: finalize});
  if (config.finalize === 'noncallable') ViewPU.prototype.finalizeConstruction = 7;
  if (config.finalize === 'frozen') Object.preventExtensions(ViewPU.prototype);
  tag(ViewPU.prototype, 'ViewPU.prototype');
  function createPath(version) {
    const path = surface(`path${paths.length}`, {pushPath(value) {
      assert.deepEqual(Reflect.ownKeys(value), ['name']); event('push', value.name); return token;
    }});
    paths.push(path); event('path.new', version, path); return path;
  }
  const pathTypes = [0, 1].map(version => function NavPathStack() {return createPath(version);});
  const observedTypes = [0, 1].map(version => function Observed(value, owner, name) {
    assert.ok(owners.includes(owner)); assert.equal(name, 'navPageInfos'); event('observed.new', version, owner, value, name);
    return createCell(owner, name, value);
  });
  const keys = ['create', 'pop', 'width', 'height', 'padding', 'fontWeight', 'fontSize', 'fontColor', 'margin',
    'backgroundColor', 'borderRadius', 'color', 'strokeWidth', 'onClick', 'layoutWeight', 'hideToolBar',
    'hideTitleBar', 'mode', 'navDestination'];
  components = [0, 1].map(version => new Map(['Navigation', 'Column', 'Text', 'ForEach', 'If', 'Divider', 'Row', 'Span', 'Image'].map(name =>
    [name, surface(`${name}${version}`, Object.fromEntries(keys.map(key => [key, (...args) => {
      if (key === 'onClick') {assert.equal(args[0].length, 0); clicks.push(args[0]);}
      if (key === 'navDestination') {assert.equal(args[0].builder.length, 1); builders.push(args[0].builder);}
      return token;
    }])))])));
  subscribers = [0, 1].map(version => {
    const instance = surface(`subscriber${version}`, {delete: () => token});
    return surface(`SubscriberManager${version}`, {Get: () => instance});
  });
  const jsonSurfaces = [0, 1].map(version => surface(`JSON${version}`, {stringify: value => JSON.stringify(value)}));
  const childTypes = [0, 1].map(version => new Map(childNames.map(name => {
    function Child(parent, params, storage, id, callback, metadata) {
      tag(this, `${name}${version}:child${children.length}`); children.push(this);
      assert.ok(owners.includes(parent)); assert.deepEqual(Reflect.ownKeys(params), []); assert.equal(storage, undefined);
      assert.equal(Reflect.apply(callback, {foreign: true}, []), undefined); event('child.new', name, version, parent, id, metadata);
    }
    Object.defineProperty(Child.prototype, 'paramsGenerator_', {set(generator) {
      assert.equal(generator.name, 'paramsLambda'); assert.equal(generator.length, 0);
      const a = Reflect.apply(generator, {foreign: true}, []), b = Reflect.apply(generator, null, []);
      assert.notEqual(a, b); assert.deepEqual(Reflect.ownKeys(a), []); assert.deepEqual(Reflect.ownKeys(b), []);
      event('child.params', this);
    }});
    return [name, Child];
  })));
  context = vm.createContext({ViewPU, NavPathStack: pathTypes[0], ObservedPropertyObjectPU: observedTypes[0],
    ...Object.fromEntries(components[0]), SubscriberManager: subscribers[0], JSON: jsonSurfaces[0],
    NavigationMode: surface('mode', {Stack: 'stack'}), FontWeight: surface('weight', {Bold: 'bold', Medium: 'medium'}),
    Color: surface('color', {White: 'white'}), Reflect: surface('Reflect', {set: (...args) => Reflect.set(...args)}),
    registerNamedRoute(callback, name, metadata) {
      assert.equal(this, undefined); assert.equal(callback.length, 0); factory = callback;
      event('route.retained', name, metadata); return token;
    },
  });
  const dependency = new vm.SourceTextModule(routes, {context});
  const module = new vm.SourceTextModule(source, {context});
  childModules = new Map();
  await module.link(specifier => {
    if (specifier === routeSpecifier) return dependency;
    const name = childNames.find(name => specifier.endsWith(`/${name}&`)); assert.ok(name, specifier);
    const imported = new vm.SyntheticModule([name], function() {this.setExport(name, childTypes[0].get(name));}, {context});
    childModules.set(name, imported); return imported;
  });
  assert.throws(() => dependency.namespace.ROUTES, {name: 'ReferenceError'});
  assert.deepEqual(Object.keys(module.namespace), []);
  try {await module.evaluate(); trace.push(['module.return']);}
  catch (error) {moduleError = error; moduleFailed = true; trace.push(['module.throw', outcome(error)]);}
  const beforeRepeat = trace.length;
  if (moduleFailed) await assert.rejects(module.evaluate(), error => Object.is(error, moduleError));
  else await module.evaluate();
  assert.equal(trace.length, beforeRepeat);
  if (factory) {
    try {const first = Reflect.apply(factory, {foreign: true}, []); assert.equal(first, owners[0]); trace.push(['factory.return']);}
    catch (error) {trace.push(['factory.throw', outcome(error)]);}
  }
  const completed = owners.filter(owner => Object.hasOwn(owner, '__windowUtil'));
  let assignment, construction;
  if (Type) {
    assert.equal(Type.name, 'Index'); assert.equal(Type.length, 3); assert.equal(Type.getEntryName(), 'Index');
    assert.equal(Object.getPrototypeOf(Type), ViewPU); assert.equal(Object.getPrototypeOf(Type.prototype), ViewPU.prototype);
    assert.deepEqual(Reflect.ownKeys(Type), ['length', 'name', 'prototype', 'getEntryName']);
    assert.deepEqual(Reflect.ownKeys(Type.prototype), ['constructor', 'setInitiallyProvidedValue', 'updateStateVars',
      'purgeVariableDependenciesOnElmtId', 'aboutToBeDeleted', 'navPageInfos', 'windowUtil', 'go',
      'NavPageMap', 'initialRender', 'rerender']);
    const constructor = Object.getOwnPropertyDescriptor(Type.prototype, 'constructor');
    assert.equal(constructor.value, Type);
    assert.deepEqual([constructor.writable, constructor.enumerable, constructor.configurable], [true, false, true]);
    assert.equal(Object.getOwnPropertyDescriptor(Type, 'prototype').writable, false);
    assert.throws(() => Reflect.apply(Type, {}, []), {name: 'TypeError'});
    assert.equal(Type.getEntryName.name, 'getEntryName'); assert.equal(Type.getEntryName.length, 0);
    const staticEntry = Object.getOwnPropertyDescriptor(Type, 'getEntryName');
    assert.deepEqual([staticEntry.writable, staticEntry.enumerable, staticEntry.configurable], [true, false, true]);
    for (const [name, length] of [['setInitiallyProvidedValue', 1], ['updateStateVars', 1],
      ['purgeVariableDependenciesOnElmtId', 1], ['aboutToBeDeleted', 0], ['go', 1], ['NavPageMap', 1], ['initialRender', 0], ['rerender', 0]]) {
      const d = Object.getOwnPropertyDescriptor(Type.prototype, name);
      assert.equal(d.value.name, name); assert.equal(d.value.length, length); assert.equal(Object.hasOwn(d.value, 'prototype'), false);
      assert.deepEqual([d.writable, d.enumerable, d.configurable], [true, false, true]);
    }
    for (const name of ['navPageInfos', 'windowUtil']) {
      const d = Object.getOwnPropertyDescriptor(Type.prototype, name);
      assert.equal(d.get.name, `get ${name}`); assert.equal(d.get.length, 0);
      assert.equal(d.set.name, `set ${name}`); assert.equal(d.set.length, 1);
      assert.deepEqual([d.enumerable, d.configurable], [false, true]);
    }
    if (config.two !== false) {
      const lambda = tag(() => ({}), 'lambda');
      const params = {navPageInfos: completed.length ? completed[0].navPageInfos : undefined};
      let reads = 0;
      if (config.changingParams) Object.defineProperty(params, 'navPageInfos', {get() {
        event('params.navPageInfos'); return ++reads === 1 ? paths[0] : paths[1] ?? createPath(0);
      }});
      let assignedReceiver, assignedValue, writes = 0;
      if (config.assignment) Object.defineProperty(Type.prototype, config.assignment, {configurable: true, set(value) {
        ++writes; assignedReceiver = this; assignedValue = value; event('field.assignment', this, config.assignment, value);
        if (config.setterThrows) throw failure;
        Object.defineProperty(this, config.assignment, {value, writable: true, enumerable: true, configurable: true});
      }});
      const alternate = Object.create(Type.prototype), Target = function() {};
      Target.prototype = alternate;
      const supplied = ['parent', params, 'storage', config.elmtId,
        Object.hasOwn(config, 'paramsLambda') ? config.paramsLambda : lambda, 'extra'];
      const args = Array.from({length: config.argumentCount ?? 6}, (_, i) => i < supplied.length ? supplied[i] : tag({}, `tail${i}`));
      try {const second = Reflect.construct(Type, args, config.alternate ? Target : Type); completed.push(second); trace.push(['second.return']);}
      catch (error) {trace.push(['second.throw', outcome(error)]);}
      construction = owners.map(owner => Reflect.ownKeys(owner));
      if (config.assignment) {
        assignment = {writes, receiver: assignedReceiver && tags.get(assignedReceiver), value: normalize(assignedValue),
          prototype: assignedReceiver && Object.getPrototypeOf(assignedReceiver) === (config.alternate ? alternate : Type.prototype)};
        delete Type.prototype[config.assignment];
      }
    }
  }
  if (dependency.status === 'evaluated') {
    const values = dependency.namespace.ROUTES;
    if (Object.hasOwn(config, 'children')) values[0].children = config.children;
    if (config.changingChildren) {
      const current = values[0].children;
      Object.defineProperty(values[0], 'children', {configurable: true, enumerable: true, get() {
        event('children.get'); return current;
      }});
      tag(values[0], 'changing-route');
    }
    for (const owner of completed) {
      const poison = new Proxy({}, {get() {assert.fail('empty update read params');}});
      call('update', owner.updateStateVars, owner, [poison]);
      const state = tag({}, `window:${tags.get(owner)}`);
      try {owner.windowUtil = state; assert.equal(owner.windowUtil, state);} catch (error) {trace.push(['accessor.throw', outcome(error)]);}
      call('go', owner.go, owner, [Object.hasOwn(config, 'to') ? config.to : 'Shopping']);
      call('render', owner.initialRender, owner);
      for (const name of config.names ?? [...childNames, 'missing']) call('navmap', owner.NavPageMap, owner, [name, null]);
      call('purge', owner.purgeVariableDependenciesOnElmtId, owner, [config.elmtId]);
      call('rerender', owner.rerender, owner); call('delete', owner.aboutToBeDeleted, owner);
    }
    if (!config.immediate) {
      while (callbacks.length) {
        const pending = callbacks.splice(0); if (config.reverse) pending.reverse();
        for (const entry of pending) replayed.push([entry.id,
          call('retained.callback', entry.callback, {foreign: true}, [entry.id, config.initial ?? true])]);
      }
    }
    if (config.changeTargets) for (const route of values) if (Array.isArray(route.children))
      for (const child of route.children) child.to = 'ShoppingAvoid';
    for (const click of clicks) call('click', click, {foreign: true});
    for (const builder of builders) for (const name of config.names ?? childNames) call('builder', builder, {foreign: true}, [name, undefined]);
    if (!config.immediate) while (callbacks.length) {
      const pending = callbacks.splice(0); if (config.reverse) pending.reverse();
      for (const entry of pending) replayed.push([entry.id,
        call('builder.callback', entry.callback, {foreign: true}, [entry.id, config.initial ?? true])]);
    }
    if (config.immediate && retainedFaultId !== undefined) {
      const retained = callbacks.find(entry => entry.id === retainedFaultId);
      assert.ok(retained);
      replayed.push([retained.id, call('registration.failed.callback', retained.callback,
        {foreign: true}, [retained.id, config.initial ?? true])]);
    }
  }
  const state = owners.map(owner => Reflect.ownKeys(owner).map(key => {
    const d = Object.getOwnPropertyDescriptor(owner, key); assert.ok('value' in d);
    return [key, normalize(d.value), d.writable, d.enumerable, d.configurable];
  }));
  const cellState = cells.map(cell => [tags.get(cell.owner), cell.alias, normalize(cell.value), cell.deleted, cell.purged]);
  return {trace, state, cellState, events, failed, moduleFailed, retained: callbacks.length,
    retainedFaultId, replayed, assignment, construction};
}

export async function verifyIndexRecordBatch(source, routes, reference, referenceRoutes, request) {
  const base = {finalize: 'own', two: false};
  const expected = await exercise(reference, referenceRoutes, base);
  const immediateExpected = await exercise(reference, referenceRoutes, {...base, immediate: true});
  const eventCounts = {deferred: expected.events, immediate: immediateExpected.events};
  const compare = async config => {
    const expected = await exercise(reference, referenceRoutes, config), actual = await exercise(source, routes, config);
    assert.deepEqual(actual, expected); return actual;
  };
  const inventory = {lifecycle: 0, faults: 0, assignments: 0, mutations: 0};
  let cases = 0, faults = 0, swaps = 0, assignments = 0, mutations = 0;
  const selected = phase => {const i = inventory[phase]++; return request.phase === phase && i >= request.first && i < request.first + request.count;};
  for (const immediate of [false, true]) for (const reverse of [false, true]) for (const initial of [false, true])
    for (const baseObject of [false, true]) for (const finalize of ['own', 'absent', 'inherited', 'noncallable', 'frozen']) {
      if (selected('lifecycle')) {await compare({immediate, reverse, initial, baseObject, finalize}); ++cases;}
    }
  for (const count of [0, 1, 2, 3, 4, 6, 200, 201]) if (selected('lifecycle')) {
    await compare({finalize: 'own', argumentCount: count}); ++cases;
  }
  for (const children of [undefined, null, false, 0, NaN, '', [], {}, 'text', 1n])
    for (const changeTargets of [false, true]) if (selected('lifecycle')) {
      await compare({...base, children, changeTargets}); ++cases;
    }
  for (const changingChildren of [false, true]) for (const immediate of [false, true]) if (selected('lifecycle')) {
    await compare({...base, changingChildren, immediate}); ++cases;
  }
  for (const changingParams of [false, true]) for (const immediate of [false, true]) if (selected('lifecycle')) {
    await compare({finalize: 'own', changingParams, immediate}); ++cases;
  }
  for (const to of [undefined, null, false, 0, -0, NaN, '', 'Shopping', 1n, Symbol('to')]) if (selected('lifecycle')) {
    await compare({...base, to}); ++cases;
  }
  for (const names of [[undefined, null, false, 0, NaN, Symbol('name')], []]) if (selected('lifecycle')) {
    await compare({...base, names}); ++cases;
  }
  for (const paramsLambda of [undefined, null, false, 0, NaN, '', 1n, Symbol('lambda')]) if (selected('lifecycle')) {
    await compare({finalize: 'own', paramsLambda}); ++cases;
  }
  for (const immediate of [false, true]) for (let failAt = 1; failAt <= (immediate ? eventCounts.immediate : eventCounts.deferred); ++failAt) {
    for (const failure of [undefined, null, 0, Symbol('failure'), {}]) if (selected('faults')) {
      const result = await compare({...base, immediate, failAt, failure}); assert.equal(result.failed, true);
      if (result.retainedFaultId !== undefined) {
        assert.ok(result.trace.some(([event, outcome]) => event.endsWith('.throw') && outcome === 'original-throw'));
        assert.ok(result.replayed.some(([id, completed]) => id === result.retainedFaultId && completed),
          'the exact retained callback was not successfully replayed after registration failed');
      }
      ++faults;
    }
    if (selected('faults')) {const result = await compare({...base, immediate, swapAt: failAt}); assert.ok(result.trace.some(([event]) => event === 'live-swap')); ++swaps;}
  }
  for (const assignment of ['paramsGenerator_', '__navPageInfos', '__windowUtil'])
    for (const alternate of [false, true]) for (const setterThrows of [false, true])
      for (const failure of setterThrows ? [undefined, null, 0, Symbol('setter'), {}] : [undefined]) if (selected('assignments')) {
        const result = await compare({finalize: 'own', assignment, alternate, setterThrows, failure});
        assert.equal(result.assignment.writes, 1); assert.equal(result.assignment.receiver, 'owner1');
        assert.equal(result.assignment.prototype, true);
        const ordered = ['paramsGenerator_', '__navPageInfos', '__windowUtil'];
        assert.deepEqual(result.construction[1], setterThrows ? ordered.slice(0, ordered.indexOf(assignment)) : ordered);
        ++assignments;
      }
  for (const [before, after] of [
    ['return $local0.go(routeItem.to);', 'return $local0.go("wrong");'],
    ['this.__navPageInfos.set(newValue);', 'this.__windowUtil.set(newValue);'],
    ['this.__windowUtil.purgeDependencyOnElmtId(rmElmtId);', 'this.__windowUtil.purgeDependencyOnElmtId(0);'],
    ['this.__navPageInfos.aboutToBeDeleted();', 'this.__navPageInfos.get();'],
    ['this.updateDirtyElements();', 'return this.updateDirtyElements();'],
    // The receiver cell may print as an assignment or as its own initializer.
    ...(source.includes('($local0 = this);') ? [['($local0 = this);', '($local0 = globalThis);']]
        : [['let $local0 = this;', 'let $local0 = globalThis;']]),
    ['elmtId = (-1)', 'elmtId = 0'],
    ['new Shopping(', 'new ShoppingAvoid('],
    ['super(parent, __localStorage, elmtId, extraInfo)', 'super(parent, params, elmtId, extraInfo)'],
    ['(this.__navPageInfos = new ObservedPropertyObjectPU(new NavPathStack(), this, "navPageInfos"));',
      'Object.defineProperty(this, "__navPageInfos", {value: new ObservedPropertyObjectPU(new NavPathStack(), this, "navPageInfos"), writable: true, enumerable: true, configurable: true});'],
  ]) {
    if (!selected('mutations')) continue;
    const changed = source.replace(before, after); assert.notEqual(changed, source); new vm.SourceTextModule(changed);
    await assert.rejects(compareMutation(changed), error => error instanceof assert.AssertionError); ++mutations;
  }
  async function compareMutation(changed) {
    const config = {finalize: 'own', elmtId: 7, assignment: '__navPageInfos'};
    const actual = await exercise(changed, routes, config);
    assert.deepEqual(actual, await exercise(reference, referenceRoutes, config));
  }
  return {inventory, cases, faults, swaps, assignments, mutations, events: eventCounts};
}

export async function verifyIndexRecord(source, routes, reference, referenceRoutes) {
  // Representation is checked separately from execution. Accept the mutable
  // class binding, receiver snapshots, equivalent if polarity and callable-name
  // envelopes; require native source constructs instead of a register machine.
  assert.match(source, /class extends \(ViewPU\)/);
  assert.match(source, /constructor\(parent, params, __localStorage, elmtId = \(-1\), paramsLambda = \(void 0\), extraInfo\)/);
  assert.match(source, /super\(parent, __localStorage, elmtId, extraInfo\)/);
  assert.match(source, /this\.__navPageInfos = new ObservedPropertyObjectPU\(new NavPathStack\(\), this, "navPageInfos"\)/);
  assert.match(source, /this\.__windowUtil = this\.createStorageLink\("windowUtil", \(void 0\), "windowUtil"\)/);
  assert.match(source, /NavPageMap\(name, parent = null\)/); assert.match(source, /initialRender\(\)/);
  assert.match(source, /get navPageInfos\(\)/); assert.match(source, /set windowUtil\(newValue\)/);
  assert.match(source, /route\.children \|\| \[\]/); assert.match(source, /routeItem\.to/);
  assert.match(source, /Navigation\.navDestination\(/); assert.match(source, /Row\.onClick\(/);
  assert.match(source, /static getEntryName\(\)/); assert.match(source, /=>/);
  assert.doesNotMatch(source, /INCOMPLETE|UNRECOVERED|\$acc\b|\$r\d+\b|\bwhile\s*\(|Reflect\.construct/);
  const worker = fileURLToPath(new URL('./index_record_worker.mjs', import.meta.url));
  const run = request => JSON.parse(execFileSync(process.execPath, ['--experimental-vm-modules', worker], {
    input: JSON.stringify({source, routes, reference, referenceRoutes, request}), encoding: 'utf8', timeout: 60_000,
    maxBuffer: 8 * 1024 * 1024, stdio: 'pipe',
  }));
  const {inventory, events} = run({phase: 'inventory', first: 0, count: 0});
  const result = {cases: 0, faults: 0, swaps: 0, assignments: 0, mutations: 0};
  const width = 64;
  for (const [phase, count] of Object.entries(inventory)) for (let first = 0; first < count; first += width) {
    const row = run({phase, first, count: Math.min(width, count - first)});
    assert.deepEqual(row.inventory, inventory); assert.deepEqual(row.events, events);
    for (const key of Object.keys(result)) result[key] += row[key];
  }
  assert.equal(result.cases, inventory.lifecycle); assert.equal(result.faults + result.swaps, inventory.faults);
  assert.equal(result.assignments, inventory.assignments);
  assert.equal(result.mutations, inventory.mutations);
  return {...result, wholeRecordBehavior: true, wholeRecordStyle: true, platformExecution: false, events};
}
