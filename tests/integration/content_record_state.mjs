import assert from 'node:assert/strict';
import vm from 'node:vm';

const members = [['setInitiallyProvidedValue', 1], ['updateStateVars', 1],
  ['purgeVariableDependenciesOnElmtId', 1], ['aboutToBeDeleted', 0],
  ['initialRender', 0], ['getBirdImages', 1], ['rerender', 0]];
const components = ['Column', 'Grid', 'ForEach', 'GridItem', 'Image'];
const failureValues = () => [undefined, null, 0, Symbol('fault'), {}];

// Execute the actual Content class, its nested closures and both generated
// peer records. Only the framework/storage/UI protocols belong to this host.
async function exercise(sources, config = {}) {
  const trace = [], owners = [], cells = [], pending = [], resources = [], resourceData = [], keys = [], states = [];
  const tags = new WeakMap();
  const failure = Object.hasOwn(config, 'failure') ? config.failure : {};
  const saved = new Map(), faultCallbacks = [];
  let events = 0, thrown = 0, context, commons, C;
  const tag = (value, name) => { tags.set(value, name); return value; };
  const encode = value => {
    if (value === null || typeof value !== 'object' && typeof value !== 'function') return value;
    if (tags.has(value)) return tags.get(value);
    if (typeof value === 'function') return ['function', value.name, value.length, Object.hasOwn(value, 'prototype')];
    return Reflect.ownKeys(value).map(key => {
      const d = Object.getOwnPropertyDescriptor(value, key);
      assert.ok('value' in d, 'unexpected unobserved accessor ' + String(key));
      return [key, encode(d.value), d.writable, d.enumerable, d.configurable];
    });
  };
  const poison = tag({get then() { assert.fail('void result must not be assimilated'); }}, 'void');
  const event = (name, ...args) => {
    trace.push([name, ...args.map(encode)]);
    if (++events === config.swapAt) {
      for (const name of components) context[name] = ui[1].get(name);
      context.SynchedPropertyNesedObjectPU = cellTypes[1];
      context.SubscriberManager = subscribers[1];
      commons.setExport('BreakpointType', breakpoints[1]);
      commons.setExport('CommonConstants', constants[1]);
      trace.push(['swap']);
    }
    if (events === config.failAt) throw failure;
  };
  const outcome = error => {
    if (Object.is(error, failure)) { ++thrown; return 'raw'; }
    if (error instanceof assert.AssertionError) throw error;
    assert.equal(error?.name, 'TypeError'); return 'TypeError';
  };
  const call = (label, body) => {
    try { const value = body(); assert.equal(value, undefined); trace.push([label + '.return']); }
    catch (error) { trace.push([label + '.throw', outcome(error)]); }
  };
  const surface = (label, values) => {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); assert.ok(Object.hasOwn(values, key), label + '.' + String(key));
      event(label + '.get', key); const member = values[key];
      if (typeof member !== 'function') return member;
      const callback = function(...args) { assert.equal(this, object); event(label + '.call', key, ...args); return member(...args); };
      for (const name of ['call', 'apply', 'bind']) Object.defineProperty(callback, name, {get() { assert.fail('ambient invocation adapter'); }});
      return callback;
    }});
    return tag(object, label);
  };
  const ui = [0, 1].map(version => new Map(components.map(name => [name,
    surface(name + version, Object.fromEntries(['create', 'pop', 'layoutWeight', 'padding', 'scrollBar', 'columnsTemplate',
      'aspectRatio', 'backgroundColor', 'borderColor', 'borderWidth', 'borderRadius', 'margin', 'width', 'height']
      .map(method => [method, (...args) => {
        if (name === 'GridItem' && method === 'create') {
          assert.equal(args[1], false); assert.equal(Reflect.apply(args[0], {wrong: true}, []), undefined);
        }
        return poison;
      }])))])));
  const subscribers = [0, 1].map(version => surface('subscriber' + version, {
    Get: () => surface('subscriber-list' + version, {delete: () => poison}),
  }));
  const cellTypes = [0, 1].map(version => {
    function SynchedPropertyNesedObjectPU(value, owner, name) {
      assert.equal(name, 'mainWindowInfo'); assert.ok(owners.includes(owner));
      event('cell' + version + '.construct', value, owner, name);
      const state = {owner, value, deleted: false, purged: []}; cells.push(state);
      const cell = surface('cell:' + (cells.length - 1), {
        get: () => state.value, set(next) { state.value = next; return poison; },
        purgeDependencyOnElmtId(id) { state.purged.push(id); return poison; },
        aboutToBeDeleted() { state.deleted = true; return poison; },
      });
      return cell;
    }
    return tag(SynchedPropertyNesedObjectPU, 'cell-type' + version);
  });
  const retain = (owner, callback, type) => {
    const entry = {owner, callback, type, id: pending.length + states.length + 1};
    pending.push(entry); if (events + 1 === config.failAt) faultCallbacks.push(entry);
    event('retain', owner, callback, type);
    if (config.immediate) {
      pending.pop(); states.push(entry.id);
      assert.equal(Reflect.apply(callback, {wrongReceiver: true}, [entry.id, config.initial !== false]), undefined);
    }
    return poison;
  };
  function ViewPU(...args) {
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    tag(owner, 'owner' + owners.length); owners.push(owner); event('super', new.target, ...args);
    return config.baseObject ? owner : undefined;
  }
  const methods = {
    finalizeConstruction() { event('finalize', this); return poison; },
    observeComponentCreation2(callback, type) { assert.ok(owners.includes(this)); return retain(this, callback, type); },
    forEachUpdateFunction(id, items, item, key, indexFlag, itemFlag) {
      assert.ok(owners.includes(this)); assert.ok(Array.isArray(items));
      assert.equal(indexFlag, false); assert.equal(itemFlag, true);
      event('forEach', this, id, items.length, item, key, indexFlag, itemFlag);
      resourceData.push(...items.map(encode));
      for (let index = 0; index < items.length; ++index) tag(items[index], 'resource' + (resources.length + index));
      resources.push(...items);
      const order = config.reverseItems ? [...items.keys()].reverse() : [...items.keys()];
      for (const index of order) {
        const identifier = Reflect.apply(key, {wrongReceiver: true}, [items[index], index]);
        keys.push(identifier); event('key', identifier);
        assert.equal(Reflect.apply(item, {wrongReceiver: true}, [items[index]]), undefined);
      }
      return poison;
    },
    id__() { event('id', this); return owners.indexOf(this) + 17; },
    aboutToBeDeletedInternal() { event('delete-internal', this); return poison; },
    updateDirtyElements() { event('update-dirty', this); return poison; },
  };
  for (const [name, body] of Object.entries(methods)) Object.defineProperty(ViewPU.prototype, name,
    {configurable: true, get() { event('base.get', name, this); return body; }});
  if (config.fallback) delete ViewPU.prototype.finalizeConstruction;
  const enums = surface('widths', {WIDTH_SM: 'sm', WIDTH_MD: 'md', WIDTH_LG: 'lg'});
  context = vm.createContext({ViewPU, WidthBreakpoint: enums, BarState: surface('bar', {Off: 0}),
    SynchedPropertyNesedObjectPU: cellTypes[0], SubscriberManager: subscribers[0],
    ...Object.fromEntries(ui[0])});
  const peers = new Map();
  for (const name of ['Breakpoint', 'CommonConstants']) {
    const module = new vm.SourceTextModule(sources[name], {context});
    await module.link(() => assert.fail('unexpected peer import')); await module.evaluate(); peers.set(name, module);
  }
  const Breakpoint = peers.get('Breakpoint').namespace.BreakpointType;
  const breakpoints = [0, 1].map(version => tag(new Proxy(Breakpoint, {construct(target, args, newTarget) {
    event('breakpoint' + version + '.construct', ...args); return Reflect.construct(target, args, newTarget);
  }}), 'breakpoint' + version));
  const Constants = peers.get('CommonConstants').namespace.CommonConstants;
  const constants = [0, 1].map(version => surface('constants' + version, {FULL_PERCENT: Constants.FULL_PERCENT}));
  commons = new vm.SyntheticModule(['BreakpointType', 'CommonConstants'], function() {
    this.setExport('BreakpointType', breakpoints[0]); this.setExport('CommonConstants', constants[0]);
  }, {context});
  const module = new vm.SourceTextModule(sources.Content, {context});
  await module.link(name => {
    assert.equal(name, '@normalized:N&&&commons/Index&1.0.0'); return commons;
  });
  assert.deepEqual(Reflect.ownKeys(module.namespace), ['Content', Symbol.toStringTag]);
  assert.throws(() => module.namespace.Content, {name: 'ReferenceError'});
  await module.evaluate(); C = module.namespace.Content;
  assert.equal(C.name, 'Content'); assert.equal(C.length, 3);
  assert.equal(Object.getPrototypeOf(C), ViewPU);
  assert.equal(Object.getPrototypeOf(C.prototype), ViewPU.prototype);
  assert.equal(C.prototype.constructor, C);
  assert.deepEqual(Reflect.ownKeys(C), ['length', 'name', 'prototype']);
  assert.deepEqual(Reflect.ownKeys(C.prototype), ['constructor', ...members.slice(0, 4).map(([name]) => name),
    'mainWindowInfo', ...members.slice(4).map(([name]) => name)]);
  assert.throws(() => Reflect.apply(C, {}, []), {name: 'TypeError'});
  for (const [name, length] of members) {
    const d = Object.getOwnPropertyDescriptor(C.prototype, name);
    assert.ok(d);
    assert.equal(d.value.name, name); assert.equal(d.value.length, length);
    assert.deepEqual([d.writable, d.enumerable, d.configurable], [true, false, true]);
    assert.equal(Object.hasOwn(d.value, 'prototype'), false);
    assert.throws(() => Reflect.construct(d.value, []), {name: 'TypeError'});
  }
  const accessor = Object.getOwnPropertyDescriptor(C.prototype, 'mainWindowInfo');
  assert.ok(accessor);
  assert.equal(accessor.get.name, 'get mainWindowInfo'); assert.equal(accessor.get.length, 0); assert.equal(accessor.set, undefined);
  assert.deepEqual([accessor.enumerable, accessor.configurable], [false, true]);
  assert.throws(() => Reflect.construct(accessor.get, []), {name: 'TypeError'});
  assert.equal(Reflect.set(module.namespace, 'Content', null), false);
  await module.evaluate(); assert.equal(module.namespace.Content, C);
  tag(C, 'Content-class');
  const finalizer = Object.getOwnPropertyDescriptor(ViewPU.prototype, 'finalizeConstruction');
  assert.ok(finalizer, 'module must install its missing finalizer');
  if (config.fallback) {
    assert.equal(finalizer.value.name, ''); assert.equal(finalizer.value.length, 0);
    assert.deepEqual([finalizer.writable, finalizer.enumerable, finalizer.configurable], [true, true, true]);
  } else assert.equal(typeof finalizer.get, 'function');
  trace.length = 0; events = 0;
  if (config.assignment) for (const field of ['paramsGenerator_', '__mainWindowInfo']) {
    const values = new WeakMap(); saved.set(field, values);
    Object.defineProperty(C.prototype, field, {configurable: true,
      get() { return values.get(this); }, set(value) { values.set(this, value); event('assign', field, value, this); }});
  }
  const parent = tag({}, 'parent'), storage = tag({}, 'storage'), extra = tag({}, 'extra');
  const lambda = tag(function paramsLambda() { return poison; }, 'lambda');
  const makeInfo = index => tag({get widthBp() { event('info.width', index); return config.width ?? 'md'; }}, 'info' + index);
  const values = [makeInfo(0), makeInfo(1)];
  const params = index => tag({get mainWindowInfo() { event('params.get', index); return values[index]; }}, 'params' + index);
  const count = config.concurrent ? 2 : 1;
  const target = config.foreignTarget ? tag(new Proxy(function AlternateTarget() {}, {get(object, key, receiver) {
    if (key === 'prototype') { event('target.prototype'); return C.prototype; }
    return Reflect.get(object, key, receiver);
  }}), 'new-target') : C;
  for (let index = 0; index < count; ++index) {
    call('construct', () => {
      const args = [parent, params(index), storage, 17 + index, config.lambda ? lambda : undefined, extra];
      if (config.arity !== undefined) {
        while (args.length < config.arity) args.push(tag({}, 'ignored' + args.length)); args.length = config.arity;
      }
      const value = Reflect.construct(C, args, target); assert.equal(value, owners.at(-1));
      assert.equal(Object.getPrototypeOf(value), C.prototype);
    });
    if (config.constructorOnly) continue;
    const owner = owners.at(-1); if (!owner) continue;
    call('initial', () => owner.initialRender());
    call('update', () => owner.updateStateVars(params(1 - index)));
    call('purge', () => owner.purgeVariableDependenciesOnElmtId(29));
    call('rerender', () => owner.rerender());
    if (config.deleteBeforeFlush) call('delete', () => owner.aboutToBeDeleted());
  }
  while (pending.length) {
    const entry = config.reverse ? pending.pop() : pending.shift(); states.push(entry.id);
    call('callback', () => Reflect.apply(entry.callback, {wrongReceiver: true}, [entry.id, config.initial !== false]));
  }
  // Registration can retain the callback before throwing. Replay the exact
  // retained occurrence; do not rebuild its scope or class as a test double.
  for (const entry of faultCallbacks) call('replay', () => Reflect.apply(entry.callback, null, [entry.id, false]));
  if (!config.constructorOnly && !config.deleteBeforeFlush)
    for (const owner of owners) call('delete', () => owner.aboutToBeDeleted());
  const own = owners.map(owner => Reflect.ownKeys(owner).map(key => {
    const d = Object.getOwnPropertyDescriptor(owner, key);
    assert.ok('value' in d); return [key, encode(d.value), d.writable, d.enumerable, d.configurable];
  }));
  const assigned = owners.map(owner => [...saved].filter(([, values]) => values.has(owner)).map(([field, values]) => [field, encode(values.get(owner))]));
  return {trace, thrown, own, assigned, states, keys, resources: resources.map(encode), resourceData,
    freshResources: new Set(resources).size, freshParams: new Set(resources.map(item => item.params)).size,
    cells: cells.map(state => [encode(state.owner), encode(state.value), state.purged, state.deleted])};
}

export async function checkContentRecord(sources, reference, request) {
  const compare = async config => {
    const actual = await exercise(sources, config), expected = await exercise(reference, config);
    assert.deepEqual(actual, expected);
    if (config.failAt) assert.ok(actual.thrown > 0, 'original fault must be observed');
    return actual;
  };
  const baselineConfigs = [{immediate: true}, {deleteBeforeFlush: true, reverse: true},
    {assignment: true, constructorOnly: true, lambda: true, foreignTarget: true, baseObject: true}];
  const baselines = await Promise.all(baselineConfigs.map(compare));
  const counts = baselines.map(row => row.trace.filter(event => !event[0].endsWith('.return') &&
    !event[0].endsWith('.throw') && event[0] !== 'swap').length);
  const lifecycle = [];
  for (const immediate of [false, true]) for (const reverse of [false, true]) for (const width of ['sm', 'md', 'lg', 'xl'])
    for (const initial of [false, true]) lifecycle.push({immediate, reverse, width, initial, reverseItems: reverse});
  for (const concurrent of [false, true]) for (const baseObject of [false, true])
    lifecycle.push({concurrent, baseObject, deleteBeforeFlush: true, reverse: true, lambda: true});
  for (const fallback of [false, true]) for (const lambda of [false, true]) lifecycle.push({fallback, lambda});
  const assignments = [];
  for (const arity of [0, 1, 2, 3, 4, 6, 200, 201]) for (const baseObject of [false, true])
    for (const lambda of [false, true]) for (const foreignTarget of [false, true])
      assignments.push({arity, baseObject, lambda, foreignTarget, assignment: true, constructorOnly: true});
  const faultRows = counts.flatMap((count, index) => Array.from({length: count}, (_, at) => ({index, point: at + 1})));
  const failures = failureValues(), kinds = failures.length + 1;
  const inventory = {lifecycle: lifecycle.length, faults: faultRows.length * kinds, assignments: assignments.length, mutations: 9};
  const result = {cases: 0, faults: 0, swaps: 0, assignments: 0, mutations: 0};
  if (request.phase === 'lifecycle') for (const config of lifecycle.slice(request.first, request.first + request.count)) { await compare(config); ++result.cases; }
  if (request.phase === 'assignments') for (const config of assignments.slice(request.first, request.first + request.count)) { await compare(config); ++result.assignments; }
  if (request.phase === 'faults') for (let at = request.first; at < Math.min(inventory.faults, request.first + request.count); ++at) {
    const row = faultRows[Math.floor(at / kinds)], kind = at % kinds, config = baselineConfigs[row.index];
    if (kind === failures.length) { await compare({...config, swapAt: row.point}); ++result.swaps; }
    else { await compare({...config, failAt: row.point, failure: failures[kind]}); ++result.faults; }
  }
  if (request.phase === 'mutations') {
    const mutations = [
      ['super(parent, __localStorage, elmtId, extraInfo)', 'super(parent, params, elmtId, extraInfo)', {}],
      ['this.__mainWindowInfo.set(params.mainWindowInfo);', 'this.__mainWindowInfo.set(void 0);', {}],
      ['widthBp)', 'heightBp)', {}], ['GridItem.aspectRatio(1)', 'GridItem.aspectRatio(2)', {}],
      ['Image.create(image)', 'Image.create(void 0)', {reverse: true}],
      ['CommonConstants.FULL_PERCENT', '"wrong"', {}],
      ['JSON.stringify(image) + index', 'JSON.stringify(image)', {}],
      ['if (!("finalizeConstruction" in ViewPU.prototype))', 'if (("finalizeConstruction" in ViewPU.prototype))', {fallback: true}],
      ['this.__mainWindowInfo = new SynchedPropertyNesedObjectPU(params.mainWindowInfo, this, "mainWindowInfo");',
        'Object.defineProperty(this, "__mainWindowInfo", {value: new SynchedPropertyNesedObjectPU(params.mainWindowInfo, this, "mainWindowInfo"), writable: true, enumerable: true, configurable: true});',
        {assignment: true, constructorOnly: true, lambda: true}],
    ];
    for (const [before, after, config] of mutations.slice(request.first, request.first + request.count)) {
      assert.ok(sources.Content.includes(before), before); const changed = sources.Content.replace(before, after);
      assert.notEqual(changed, sources.Content, before);
      new vm.SourceTextModule(changed);
      await assert.rejects(async () => assert.deepEqual(await exercise({...sources, Content: changed}, config),
        await exercise(reference, config)), error => error instanceof assert.AssertionError); ++result.mutations;
    }
  }
  return {counts, inventory, ...result};
}
