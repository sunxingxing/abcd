import assert from 'node:assert/strict';
import vm from 'node:vm';
import {types} from 'node:util';
import {GraphTrace} from './graph_trace.mjs';
import {progressbarPlatform} from './progressbar_platform.mjs';

const failures = [undefined, null, 0, Symbol('original throw'), {}];
const page = 'entry/src/main/ets/pages/ProgressBar', constants = 'entry/src/main/ets/common/CommonConstants';
const commonImport = '@bundle:com.example.pasteboard/entry/ets/common/CommonConstants';
const components = ['NavDestination', 'Column', 'Row', 'Text', 'Progress', 'PasteButton', 'Button', 'SymbolGlyph', 'If'];

// Execute module initialization, the real exported builder and native class,
// UI callbacks, progress/timers and teardown. Keep source bodies unchanged;
// independent field/cell/file/Promise snapshots complement the complete trace.
export async function exerciseProgressbar(sources, config = {}) {
  const modules = new Map(), imports = new Map(), owners = [], cells = [], observations = [], clicks = [], ready = [], queue = [];
  const children = new Map(), builders = [], allocations = [];
  let context, activeOwner, currentElement = 0, work = 0;
  const trace = new GraphTrace({...config, ...(config.failureIndex === undefined ? {} : {failure: failures[config.failureIndex]})},
    ordinal => {if (ordinal === config.swapAt) {
      for (const [name, dependency] of imports) dependency.setExport('default', platform.load(name, 1));
      trace.trace.push(['imports.swap']);
    }});
  const token = trace.tag({get then() {assert.fail('void host result assimilated');}}, 'void');
  const platform = progressbarPlatform(trace, config, token);
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
  const render = owner => {call('view.render', owner.initialRender, owner);};
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
  for (const [name, members] of Object.entries({FlexAlign: {SpaceBetween: 1, Start: 2}, VerticalAlign: {Top: 1},
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
  context = vm.createContext(sandbox); platform.queue.observeReactions(vm.runInContext('Promise.prototype', context));
  for (const [name, source] of Object.entries(sources)) modules.set(name, new vm.SourceTextModule(source, {context, identifier: name}));
  const dependency = name => {
    if (name === commonImport) return modules.get(constants);
    if (!imports.has(name)) imports.set(name, new vm.SyntheticModule(['default'], function() {this.setExport('default', platform.load(name));}, {context}));
    return imports.get(name);
  };
  for (const module of modules.values()) if (module.status === 'unlinked') await module.link(dependency);
  for (const module of modules.values()) {
    try {await module.evaluate();} catch (error) {trace.trace.push(['module.throw', module.identifier, trace.outcome(error)]);}
  }
  const exported = trace.exports(modules), root = modules.get(page);
  if (root.status === 'evaluated') {
    if (config.superProxy) {
      const Type = root.namespace.ProgressBar, original = Object.getPrototypeOf(Type);
      const proxy = trace.tag(new Proxy(original, {construct(target, args, newTarget) {
        trace.event('platform.super.proxy', args, newTarget); return Reflect.construct(target, args, newTarget);
      }}), 'super.proxy');
      Object.setPrototypeOf(Type, proxy);
    }
    const parent = new ViewPU(), Builder = builders[0]?.builder.builder ?? root.namespace.ProgressBarBuilder;
    call('builder.call', Builder, config.parent === 'null' ? parent : {foreign: true}, [config.parent === 'null' ? null : parent]); drain();
    if (config.construct) {
      const Type = root.namespace.ProgressBar, given = config.construct === 'given';
      const values = given ? {progressInfo: 100, gradientColor: new LinearGradient([]),
        controller: new RichEditorController(), options: trace.tag({}, 'provided.options'), pathStack: new NavPathStack()}
        : {progressInfo: undefined, gradientColor: undefined, controller: undefined, options: undefined, pathStack: undefined};
      const params = config.parameterAccess ? trace.surface('provided.params', values) : values;
      const lambda = trace.tag(() => ({source: 'parameters'}), 'provided.lambda');
      const args = [parent, params, trace.tag({}, 'provided.storage'),
        Object.hasOwn(config, 'id') ? config.id : undefined, config.lambda === false ? undefined : lambda, 'provided.extra'];
      const Target = config.newTarget ? class Alternate extends Type {} : Type;
      if (config.newTarget) trace.tag(Target, 'alternate.newTarget');
      const instance = call('class.construct', () => Reflect.construct(Type, args, Target), undefined);
      if (instance) {render(instance); drain();}
    }
    for (const callback of ready) call('destination.ready', callback, {foreign: true}, [{pathStack: new NavPathStack()}]);
    for (const entry of config.reverse ? [...clicks].reverse() : clicks)
      call('click.' + entry.name, entry.callback, {foreign: true}, [trace.tag({}, 'click.event'), Object.hasOwn(config, 'result') ? config.result : 0]);
    if (!config.progressAfter) platform.progress(call);
    await platform.queue.finish();
    if (config.progressAfter) platform.progress(call);
    platform.timers(call); drain(); await platform.queue.finish();
    for (const entry of observations.filter(entry => children.has(entry.args[0]))) invoke(entry);
    for (const owner of owners.slice(1)) {
      if (typeof owner.paramsGenerator_ === 'function') call('params.generator', owner.paramsGenerator_, {foreign: true});
      if (config.provided) call('params.update', owner.setInitiallyProvidedValue, owner, [config.provided]);
      if (config.renderAgain) {render(owner); drain();}
      for (const method of ['rerender', 'purgeVariableDependenciesOnElmtId', 'aboutToBeDeleted'])
        call('view.' + method, owner[method], owner, [17]);
    }
  }
  await platform.queue.finish();
  const heritage = root.status === 'evaluated' ? [trace.normalize(Object.getPrototypeOf(root.namespace.ProgressBar)),
    trace.normalize(Object.getPrototypeOf(root.namespace.ProgressBar.prototype))] : [];
  const fields = owner => Reflect.ownKeys(owner).map(key => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, key);
    return 'value' in descriptor ? [key, trace.normalize(descriptor.value), descriptor.writable, descriptor.enumerable, descriptor.configurable]
      : [key, trace.normalize(descriptor.get), trace.normalize(descriptor.set), descriptor.enumerable, descriptor.configurable];
  });
  return {exported, heritage, trace: trace.trace, sites: trace.sites, events: trace.events, failed: trace.failed,
    diagnostics: trace.errors.length, modules: [...modules].map(([name, module]) => [name, module.status]),
    owners: owners.map(owner => [owner.constructor.name, fields(owner)]),
    cells: cells.map(({owner, ...entry}) => [trace.normalize(owner), entry]), platform: platform.snapshot(),
    clicks: clicks.map(({name}) => name), builders: builders.map(({name}) => name)};
}
