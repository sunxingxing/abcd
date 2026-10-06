import assert from 'node:assert/strict';
import {types} from 'node:util';

// Shared controlled ViewPU boundary, never an application body. Retain original
// callbacks before observable registration, replay with a foreign receiver,
// track exact async returns and distinguish every allocation's identity.
export function viewProtocol(trace, config, platform, token, components) {
  const owners = [], cells = [], observations = [], clicks = [], ready = [], queue = [];
  const children = new Map(), builders = [], allocations = [];
  let activeOwner, currentElement = 0, work = 0;
  const call = (label, fn, owner, args = []) => {
    const result = trace.call(label, fn, owner, args); if (types.isPromise(result)) platform.queue.track(result); return result;
  };
  const cell = (value, owner, key) => {
    const entry = {value, owner, key, purged: [], deleted: false}; cells.push(entry);
    return trace.surface('cell' + cells.length, {
      get: () => entry.value, set(value) {entry.value = value; return token;},
      purgeDependencyOnElmtId(id) {entry.purged.push(id); return token;},
      aboutToBeDeleted() {entry.deleted = true; return token;},
    });
  };
  function ViewPU(...args) {
    const owner = config.baseObject ? Object.create(new.target.prototype) : this;
    trace.tag(owner, new.target.name + ':' + owners.length); owners.push(owner);
    if (args[0]) children.set(args[2], owner);
    trace.event('view.super', new.target.name, ...args); return config.baseObject ? owner : undefined;
  }
  const invoke = entry => {
    const previous = activeOwner; activeOwner = entry.owner;
    call('retained.' + entry.kind, entry.callback, {foreign: true}, entry.args); activeOwner = previous;
  };
  const enqueue = entry => {
    queue.push(entry); trace.event('platform.retained', entry.kind, entry.owner, entry.callback);
    if (config.immediate) {queue.pop(); invoke(entry);}
  };
  const drain = () => {while (work < queue.length) invoke(queue[work++]);};
  const rendered = [];
  const render = owner => {
    if (typeof owner.aboutToAppear === 'function') call('view.appear', owner.aboutToAppear, owner);
    call('view.render', owner.initialRender, owner); rendered.push(owner);
  };
  trace.tag(ViewPU, 'ViewPU'); trace.tag(ViewPU.prototype, 'ViewPU.prototype');
  const methods = {
    finalizeConstruction: () => token,
    createStorageProp(key, value, alias) {return cell(Object.hasOwn(config, 'breakpoint') ? config.breakpoint : value, this, alias);},
    observeComponentCreation2(callback, component) {
      const id = ++currentElement, entry = {kind: 'observe', owner: this, callback, args: [id, true]};
      observations.push({...entry, args: [id, false]}); enqueue(entry); return token;
    },
    updateStateVarsOfChildByElmtId(id, params) {
      const child = children.get(id); if (child) call('child.update', child.updateStateVars, child, [params]); return token;
    },
    ifElseBranchUpdateFunction(branch, callback) {call('branch.' + branch, callback, {foreign: true}); return token;},
    id__() {return owners.indexOf(this);}, aboutToBeDeletedInternal: () => token,
    updateDirtyElements: () => token, getUIContext: () => platform.ui,
  };
  for (const [name, body] of Object.entries(methods)) Object.defineProperty(ViewPU.prototype, name, {configurable: true, get() {
    const owner = this; assert.ok(owners.includes(owner)); trace.event('view.get', name, owner);
    return function(...args) {assert.equal(this, owner); trace.event('view.call', name, owner, ...args); return Reflect.apply(body, owner, args);};
  }});
  if (config.finalize === 'missing') delete ViewPU.prototype.finalizeConstruction;
  ViewPU.create = owner => {trace.event('child.create', owner); render(owner); return token;};
  const valueType = name => function(...args) {
    // Distinct allocation identity is observable through controller/options and
    // old/new path stacks. The number of owning views cannot identify objects.
    trace.tag(this, name + ':' + allocations.length); allocations.push(this);
    trace.event('platform.new', name, ...args);
  };
  const NavPathStack = valueType('path'), LinearGradient = valueType('gradient'), RichEditorController = valueType('editor');
  const sandbox = {ViewPU, NavPathStack, LinearGradient, RichEditorController,
    ObservedPropertySimplePU: function(value, owner, name) {return cell(value, owner, name);},
    AppStorage: trace.surface('storage', {get(key) {assert.equal(key, 'uiContext'); return config.noContext ? undefined : platform.ui;}}),
    SubscriberManager: trace.surface('subscriber', {Get: () => trace.surface('subscriber.set', {delete: () => token})}),
    setTimeout(callback, delay) {return platform.timeout(callback, delay);},
  };
  for (const [name, members] of Object.entries({FlexAlign: {SpaceBetween: 1, Start: 2, End: 3}, VerticalAlign: {Top: 1},
    ProgressType: {Ring: 1}, PasteButtonOnClickResult: {SUCCESS: 0}})) sandbox[name] = trace.surface(name, members);
  if (config.registration !== false) {
    sandbox.NavigationBuilderRegister = function(name, builder) {builders.push({name, builder}); trace.event('builder.register', name, builder);};
    sandbox.wrapBuilder = function(builder) {trace.event('builder.wrap', builder); return {builder};};
  }
  for (const name of components) {
    let object;
    object = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, object); trace.event('ui.get', name, key);
      return function(...args) {
        assert.equal(this, object); trace.event('ui.call', name, key, ...args);
        if (key === 'onClick') {clicks.push({name, owner: activeOwner, callback: args[0]}); trace.event('platform.click.listener', name, activeOwner, args[0]);}
        if (name === 'NavDestination' && key === 'onReady') ready.push(args[0]);
        if (name === 'NavDestination' && key === 'create' && typeof args[0] === 'function')
          enqueue({kind: 'destination', callback: args[0], owner: activeOwner, args: []});
        return token;
      };
    }}); sandbox[name] = trace.tag(object, name);
  }
  Object.assign(sandbox, platform.globals ?? {});
  const fields = owner => Reflect.ownKeys(owner).map(key => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, key);
    return 'value' in descriptor ? [key, trace.normalize(descriptor.value), descriptor.writable, descriptor.enumerable, descriptor.configurable]
      : [key, trace.normalize(descriptor.get), trace.normalize(descriptor.set), descriptor.enumerable, descriptor.configurable];
  });
  return {sandbox, ViewPU, owners, cells, observations, clicks, ready, children, builders,
    rendered, call, invoke, drain, render, fields, allocate: valueType};
}
