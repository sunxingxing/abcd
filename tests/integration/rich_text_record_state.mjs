import vm from 'node:vm';
import {GraphTrace, realmIntrinsics, snapshotValues} from './graph_trace.mjs';
import {viewProtocol} from './view_protocol.mjs';
import {richTextPlatform} from './rich_text_platform.mjs';

const failures = [undefined, null, 0, Symbol('original throw'), {}];
const prefix = 'entry/src/main/ets/', constants = prefix + 'common/CommonConstants';
const components = ['NavDestination', 'Column', 'Row', 'Text', 'PasteButton', 'Button', 'RichEditor'];

// Actual five-module graph. ShareDirect's original navigation callbacks call
// the registered PageA/B builders; no child class/body is substituted. Observe
// exact native returns from discarded async methods without changing bodies.
export async function exerciseRichText(sources, config = {}) {
  const modules = new Map(), imports = new Map(); let context, nextReady = 0;
  const trace = new GraphTrace({...config, ...(config.failureIndex === undefined ? {} : {failure: failures[config.failureIndex]})},
    ordinal => {if (ordinal === config.swapAt) {
      for (const [name, dependency] of imports) dependency.setExport('default', platform.load(name, 1));
      trace.trace.push(['imports.swap']);
    }});
  const token = trace.tag({get then() {assert.fail('void SDK result assimilated');}}, 'void');
  const platform = richTextPlatform(trace, config, token), protocol = viewProtocol(trace, config, platform, token, components);
  const {sandbox, ViewPU, owners, cells, observations, clicks, ready, children, builders, call, invoke, drain, render, fields} = protocol;
  context = vm.createContext(sandbox); platform.queue.observeReactions(vm.runInContext('Promise.prototype', context));
  const intrinsics = realmIntrinsics(context);
  for (const [name, source] of Object.entries(sources)) modules.set(name, new vm.SourceTextModule(source, {context, identifier: name}));
  const dependency = name => {
    if (name === '@bundle:com.example.pasteboard/entry/ets/common/CommonConstants') return modules.get(constants);
    if (!imports.has(name)) imports.set(name, new vm.SyntheticModule(['default'], function() {this.setExport('default', platform.load(name));}, {context}));
    return imports.get(name);
  };
  for (const module of modules.values()) if (module.status === 'unlinked') await module.link(dependency);
  for (const module of modules.values()) {
    try {await module.evaluate();} catch (error) {trace.trace.push(['module.throw', module.identifier, trace.outcome(error)]);}
  }
  const exported = trace.exports(modules);
  const root = new ViewPU();
  const registered = name => builders.find(entry => entry.name === name)?.builder.builder;
  const open = (name, Builder) => {
    call('builder.' + name, Builder, config.parent === 'null' ? root : {foreign: true}, [config.parent === 'null' ? null : root]); drain();
  };
  // Observe exact native returns with an apply-only Proxy. Keep the target
  // body, receiver and signature; the observer has a distinct callable identity.
  // Export identities are snapshotted first; this graph does not claim method
  // identity observations through the proxy. Own discarded html rejections too.
  const Type = modules.get(prefix + 'pages/PasteBoard').namespace.PasteBoard;
  const original = Type.prototype.handlerPasteHtmlData;
  Type.prototype.handlerPasteHtmlData = new Proxy(original, {apply(target, receiver, args) {
    return platform.queue.track(Reflect.apply(target, receiver, args));
  }});
  platform.navigator(name => {const Builder = registered(name); if (Builder) open(name, Builder);});
  for (const [record, name] of [['PasteBoard', 'PasteBoardBuilder'], ['ShareDitect', 'ShareDirectBuilder']]) {
    const module = modules.get(prefix + 'pages/' + record);
    if (module.status === 'evaluated') open(name, registered(name) ?? module.namespace[name]);
  }
  const readyViews = () => {
    while (nextReady < ready.length) call('destination.ready', ready[nextReady++], {foreign: true}, [{pathStack: new sandbox.NavPathStack()}]);
  };
  readyViews(); await platform.queue.finish();
  if (config.directHtml !== undefined) for (const owner of owners)
    if (typeof owner.handlerPasteHtmlData === 'function') call('html.direct', owner.handlerPasteHtmlData, owner, [config.directHtml]);
  const initial = [...clicks];
  for (const entry of config.reverse ? initial.reverse() : initial)
    call('click.' + entry.name, entry.callback, {foreign: true}, [trace.tag({}, 'click.event'), Object.hasOwn(config, 'result') ? config.result : 0]);
  readyViews(); await platform.queue.finish();
  for (const entry of clicks.slice(initial.length)) call('child.click', entry.callback, {foreign: true}, []);
  await platform.queue.finish();
  for (const entry of observations.filter(entry => children.has(entry.args[0]))) invoke(entry);
  for (const owner of owners.slice(1)) {
    if (typeof owner.paramsGenerator_ === 'function') call('params.generator', owner.paramsGenerator_, {foreign: true});
    if (config.provided) call('params.update', owner.setInitiallyProvidedValue, owner, [config.provided]);
    if (config.renderAgain) {render(owner); drain();}
    for (const method of ['rerender', 'purgeVariableDependenciesOnElmtId', 'aboutToBeDeleted'])
      call('view.' + method, owner[method], owner, [17]);
  }
  await platform.queue.finish();
  const heritage = [...modules].flatMap(([name, module]) => module.status === 'evaluated'
    ? Object.entries(module.namespace).filter(([, Type]) => typeof Type === 'function' && Type.prototype)
      .map(([key, Type]) => [name, key, trace.normalize(Object.getPrototypeOf(Type)), trace.normalize(Object.getPrototypeOf(Type.prototype))]) : []);
  return {exported, heritage, trace: trace.trace, sites: trace.sites, events: trace.events, failed: trace.failed,
    diagnostics: trace.errors.length, modules: [...modules].map(([name, module]) => [name, module.status]),
    owners: owners.map(owner => [owner.constructor.name, fields(owner)]), rendered: protocol.rendered.map(owner => owner.constructor.name),
    cells: cells.map(({owner, ...entry}) => [trace.normalize(owner), entry]), platform: platform.snapshot(),
    clicks: clicks.map(({name, owner}) => [name, owner?.constructor.name]), builders: builders.map(({name}) => name),
    identities: snapshotValues({owners, cells,
      exports: [...modules].filter(([, module]) => module.status === 'evaluated').map(([name, module]) => [name, module.namespace])}, intrinsics)};
}
