import assert from 'node:assert/strict';
import vm from 'node:vm';
import {GraphTrace, realmIntrinsics, snapshotValues} from './graph_trace.mjs';
import {progressbarPlatform} from './progressbar_platform.mjs';
import {viewProtocol} from './view_protocol.mjs';

const failures = [undefined, null, 0, Symbol('original throw'), {}];
const page = 'entry/src/main/ets/pages/ProgressBar', constants = 'entry/src/main/ets/common/CommonConstants';
const commonImport = '@bundle:com.example.pasteboard/entry/ets/common/CommonConstants';
const components = ['NavDestination', 'Column', 'Row', 'Text', 'Progress', 'PasteButton', 'Button', 'SymbolGlyph', 'If'];

// Execute module initialization, the real exported builder and native class,
// UI callbacks, progress/timers and teardown. Keep source bodies unchanged;
// independent field/cell/file/Promise snapshots complement the complete trace.
export async function exerciseProgressbar(sources, config = {}) {
  const modules = new Map(), imports = new Map();
  let context;
  const trace = new GraphTrace({...config, ...(config.failureIndex === undefined ? {} : {failure: failures[config.failureIndex]})},
    ordinal => {if (ordinal === config.swapAt) {
      for (const [name, dependency] of imports) dependency.setExport('default', platform.load(name, 1));
      trace.trace.push(['imports.swap']);
    }});
  const token = trace.tag({get then() {assert.fail('void host result assimilated');}}, 'void');
  const platform = progressbarPlatform(trace, config, token);
  const protocol = viewProtocol(trace, config, platform, token, components);
  const {sandbox, ViewPU, owners, cells, observations, clicks, ready, children, builders, call, invoke, drain, render, fields} = protocol;
  const {NavPathStack, LinearGradient, RichEditorController} = sandbox;
  context = vm.createContext(sandbox); platform.queue.observeReactions(vm.runInContext('Promise.prototype', context));
  const intrinsics = realmIntrinsics(context);
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
  return {exported, heritage, trace: trace.trace, sites: trace.sites, events: trace.events, failed: trace.failed,
    diagnostics: trace.errors.length, modules: [...modules].map(([name, module]) => [name, module.status]),
    owners: owners.map(owner => [owner.constructor.name, fields(owner)]),
    cells: cells.map(({owner, ...entry}) => [trace.normalize(owner), entry]), platform: platform.snapshot(),
    clicks: clicks.map(({name}) => name), builders: builders.map(({name}) => name),
    identities: snapshotValues({owners, cells,
      exports: [...modules].filter(([, module]) => module.status === 'evaluated').map(([name, module]) => [name, module.namespace])}, intrinsics)};
}
