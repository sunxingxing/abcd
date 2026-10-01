import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {dirname, join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, abc] = process.argv.slice(2);
assert.equal(createHash('sha256').update(readFileSync(abc)).digest('hex'),
  '854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e');
// The production process receives ABC alone and exits before the oracle reads
// the full paired module. Neither function fragments nor answers enter output.
const report = JSON.parse(execFileSync(tool,
  [abc, '7283', '--record-source', '--value-source'], {encoding: 'utf8', timeout: 60_000}));
assert.equal(report.record, 6897); assert.equal(report.source_complete, true);
assert.equal(report.route, 'value_tree'); assert.equal(report.legacy_writer_used, false);
assert.equal(report.executable, false); // This test does not grant a platform receipt.
const paired = readFileSync(join(dirname(abc),
  'pasteboard_src/src/main/ets/pages/PageA.ts'), 'utf8');
// Converted ArkTS uses erase-only private type declarations. Node's stripper
// otherwise leaves JS fields (and undefined writes), unlike legacy TS field
// erasure. Remove only these two uninitialized, single-line type declarations;
// fail on any other private form rather than silently changing the oracle.
const declarations = paired.match(/^    private [A-Za-z_$][\w$]*: [^;=\n]+;$/gm);
assert.equal(declarations?.length, 2);
const erased = paired.replace(/^    private [A-Za-z_$][\w$]*: [^;=\n]+;\n/gm, '');
assert.ok(!/\bprivate\b/.test(erased));
const reference = stripTypeScriptTypes(erased);
let scenarios = 0;

async function exercise(source, options) {
  const {finalize, registration, initial, deferred, breakpoint, failure, asyncMode, paramsMode} = options;
  const trace = [], pending = [], clicks = [], ready = [], tags = new WeakMap();
  const exception = Symbol('same exception'), other = Symbol('other value');
  let current, target, element = 0, registered, factory, controller;
  const tag = (value, name) => { tags.set(value, name); return value; };
  const normalize = value => {
    if (value === exception) return 'exception';
    if (value === other) return 'other';
    if (typeof value === 'function') return ['function', value.name, value.length];
    if (!value || typeof value !== 'object') return value;
    if (tags.has(value)) return tags.get(value);
    if (Array.isArray(value)) return Array.from(value, normalize);
    const result = {};
    for (const key of Reflect.ownKeys(value)) {
      assert.equal(typeof key, 'string');
      const descriptor = Object.getOwnPropertyDescriptor(value, key);
      assert.equal(descriptor.enumerable, true); assert.equal(descriptor.writable, true);
      assert.equal(descriptor.configurable, true);
      result[key] = normalize(descriptor.value);
    }
    return result;
  };
  const event = (name, ...args) => {
    trace.push([name, ...args.map(normalize)]);
    if (name === failure) throw exception;
  };
  const method = (name, owner, body = () => undefined) => {
    const fn = function(...args) {
      assert.equal(this, owner); event(`${name}:call`, ...args); return body(...args);
    };
    for (const property of ['call', 'apply', 'bind']) Object.defineProperty(fn, property, {
      get() { assert.fail(`ambient ${property} adapter`); }
    });
    return fn;
  };
  function surface(name, bodies) {
    let proxy;
    proxy = new Proxy({}, {get(_, key) {
      assert.equal(typeof key, 'string'); event(`${name}.${key}:get`);
      assert.ok(Object.hasOwn(bodies, key), `${name}.${key}`);
      return typeof bodies[key] === 'function' ? method(`${name}.${key}`, proxy, bodies[key]) : bodies[key];
    }});
    tag(proxy, name); return proxy;
  }
  class Observed {
    constructor(value) { this.value = value; tag(this, 'observed'); }
    get() { event('storage.get'); return this.value; }
    set(value) { event('storage.set', value); this.value = value; }
    purgeDependencyOnElmtId(id) { event('storage.purge', id); }
    aboutToBeDeleted() { event('storage.delete'); }
  }
  class NavPathStack { constructor() { event('path.create'); tag(this, 'new stack'); } }
  class ViewPU {
    constructor(parent, storage, id, extra) {
      current = this; target = new.target; tag(this, 'page'); event('super', parent, storage, id, extra);
    }
    createStorageProp(name, value, key) {
      assert.equal(this, current); event('storage.create', name, value, key); return new Observed(breakpoint);
    }
    observeComponentCreation2(callback, component) {
      assert.equal(this, current); observe(this, callback, component);
    }
    id__() { assert.equal(this, current); event('id'); return 17; }
    aboutToBeDeletedInternal() { assert.equal(this, current); event('delete.internal'); }
    updateDirtyElements() { assert.equal(this, current); event('dirty'); }
    getUIContext() { assert.equal(this, current); event('ui.context'); return ui; }
    static create(component) { assert.equal(this, ViewPU); event('view.create', component); }
  }
  if (finalize) ViewPU.prototype.finalizeConstruction = function() {
    assert.equal(this, current); event('finalize');
  };
  function observe(owner, callback, component) {
    assert.equal(callback.length, 2); event('observe', component);
    const id = ++element;
    const invoke = () => Reflect.apply(callback, {wrongReceiver: true}, [id, initial]);
    if (deferred) pending.push(invoke); else invoke();
  }
  const parent = tag({
    observeComponentCreation2(callback, component) { assert.equal(this, parent); observe(this, callback, component); },
    updateStateVarsOfChildByElmtId(id, value) { assert.equal(this, parent); event('child.update', id, value); }
  }, 'parent');
  const stack = tag({}, 'provided stack');
  let reads = 0;
  const params = {get pathStack() {
    event('params.pathStack:get', ++reads);
    return paramsMode === 'absent' ? undefined : paramsMode === 'null' ? null :
      paramsMode === 'changed' && reads % 2 === 0 ? other : stack;
  }};
  const subscriber = surface('subscriber', {delete: id => undefined});
  const prompt = surface('prompt', {showToast: value => undefined});
  const ui = surface('ui', {getPromptAction: () => prompt});
  const data = tag({}, 'paste data');
  const system = surface('system', {setData: value => {
    assert.equal(value, data);
    if (asyncMode === 'value') return other;
    if (asyncMode === 'resolved') return Promise.resolve(other);
    if (asyncMode === 'deferred' || asyncMode === 'rejected') return new Promise((resolve, reject) => {
      controller = {resolve, reject};
    });
    if (asyncMode === 'thenable') return {get then() {
      event('then:get'); return function(resolve) { event('then:call'); resolve(other); };
    }};
    if (asyncMode === 'throwing-then') return {get then() { event('then:get'); throw exception; }};
    assert.fail(`unknown async mode ${asyncMode}`);
  }});
  const pasteboard = surface('pasteboard', {
    createData: (type, text) => { assert.equal(type, 'uri'); assert.equal(text, 'PageABuilder'); return data; },
    MIMETYPE_TEXT_URI: 'uri', getSystemPasteboard: () => system
  });
  class BreakpointType {
    constructor(sm, md, lg) { event('breakpoint.create', sm, md, lg); this.values = {sm, md, lg}; }
    GetValue(key) { event('breakpoint.value', key); return this.values[key]; }
  }
  const constants = {BREAK_POINT_SM: 'sm', FULL_PERCENT: '100%'};
  const nav = surface('NavDestination', {
    create: (builder, info) => {
      assert.equal(builder.length, 0);
      const invoke = () => Reflect.apply(builder, {wrongReceiver: true}, []);
      if (deferred) pending.push(invoke); else invoke();
    },
    onReady: callback => { assert.equal(callback.length, 1); ready.push(callback); },
    title: value => undefined, width: value => undefined, height: value => undefined,
    padding: value => undefined, backgroundColor: value => undefined, pop: () => undefined
  });
  const column = surface('Column', {
    create: () => undefined, justifyContent: value => undefined, height: value => undefined,
    width: value => undefined, padding: value => undefined, pop: () => undefined
  });
  const button = surface('Button', {
    createWithLabel: value => undefined, fontSize: value => undefined, width: value => undefined,
    height: value => undefined, backgroundColor: value => undefined, pop: () => undefined,
    onClick: callback => { assert.equal(callback.length, 0); clicks.push(callback); }
  });
  const globals = {
    ViewPU, NavPathStack, NavDestination: nav, Column: column, Button: button, FlexAlign: {End: 'end'},
    SubscriberManager: surface('SubscriberManager', {Get: () => subscriber}),
    wrapBuilder(callback) { assert.equal(this, undefined); factory = callback; event('wrap'); return 'wrapped'; }
  };
  if (registration === 'function') globals.NavigationBuilderRegister = function(name, wrapped) {
    assert.equal(this, undefined); event('register', name, wrapped); registered = name;
  };
  else if (registration === 'object') globals.NavigationBuilderRegister = {};
  const context = vm.createContext(globals);
  // Inherited setters detect accidentally introduced TS field writes and prove
  // the recovered own object literals do not invoke prototype setters.
  vm.runInContext(`Object.defineProperty(Object.prototype, 'offset', {
    set() { throw new Error('literal used inherited setter'); }
  });`, context);
  const module = new vm.SourceTextModule(source, {context});
  const imports = new Set();
  await module.link(specifier => {
    imports.add(specifier);
    if (specifier === '@ohos:pasteboard') return new vm.SyntheticModule(['default'], function() {
      this.setExport('default', pasteboard);
    }, {context});
    assert.equal(specifier, '@bundle:com.example.pasteboard/entry/ets/common/CommonConstants');
    return new vm.SyntheticModule(['BreakpointType', 'CommonConstants'], function() {
      this.setExport('BreakpointType', BreakpointType); this.setExport('CommonConstants', constants);
    }, {context});
  });
  let thrown = false;
  try {
    await module.evaluate();
    assert.deepEqual(Object.keys(module.namespace), ['PageA', 'PageABuilder']);
    const {PageA, PageABuilder} = module.namespace;
    assert.equal(PageA.name, 'PageA'); assert.equal(PageA.length, 3);
    assert.equal(PageABuilder.name, 'PageABuilder'); assert.equal(PageABuilder.length, 0);
    assert.equal(Object.getPrototypeOf(PageA), ViewPU);
    assert.equal(Object.getPrototypeOf(PageA.prototype), ViewPU.prototype);
    assert.throws(() => PageA(), error => error.name === 'TypeError');
    if (factory) assert.equal(factory, PageABuilder);
    const generator = function paramsLambda() { return {}; };
    const instance = new PageA(parent, params, 'storage', undefined, generator, 'extra');
    assert.equal(target, PageA); assert.equal(instance.paramsGenerator_, generator);
    assert.equal(reads, paramsMode === 'absent' ? 1 : 2);
    if (paramsMode === 'absent') assert.ok(instance.pathStack instanceof NavPathStack);
    else assert.equal(instance.pathStack, paramsMode === 'null' ? null : paramsMode === 'changed' ? other : stack);
    assert.deepEqual(Reflect.ownKeys(instance), ['paramsGenerator_', '__curBp', 'pathStack']);
    assert.equal(instance.curBp, breakpoint); instance.curBp = breakpoint;
    instance.updateStateVars({}); instance.purgeVariableDependenciesOnElmtId(9);
    instance.initialRender();
    while (pending.length) pending.shift()();
    assert.equal(clicks.length, 1); assert.equal(ready.length, 1);
    const readyContext = {get pathStack() { event('ready.pathStack:get'); return stack; }};
    assert.equal(Reflect.apply(ready[0], {wrongReceiver: true}, [readyContext]), undefined);
    assert.equal(instance.pathStack, stack);
    trace.push(['click:start']);
    const promise = Reflect.apply(clicks[0], {wrongReceiver: true}, []);
    assert.equal(typeof promise.then, 'function');
    const completion = promise.then(value => { assert.equal(value, undefined); return false; }, error => {
      assert.equal(error, exception, 'async exception identity'); return true;
    });
    trace.push(['click:returned']);
    assert.ok(!trace.some(entry => entry[0] === 'ui.context'), 'UI ran before await continuation');
    if (controller) {
      await Promise.resolve(); trace.push(['settle']);
      if (asyncMode === 'rejected') controller.reject(exception); else controller.resolve(other);
    }
    const rejected = await completion; trace.push([rejected ? 'click:rejected' : 'click:fulfilled']);
    if (rejected) {
      if (failure) assert.ok(trace.some(entry => entry[0] === failure), `failure ${failure} was never reached`);
      return {trace, imports: [...imports], registered};
    }
    instance.rerender(); instance.aboutToBeDeleted();
    trace.push(['builder:start']);
    assert.equal(Reflect.apply(PageABuilder, parent, []), undefined);
    while (pending.length) pending.shift()();
    if (initial) { assert.equal(current.paramsGenerator_.name, 'paramsLambda'); assert.equal(current.paramsGenerator_.length, 0); }
    trace.push(['builder:explicit']);
    assert.equal(Reflect.apply(PageABuilder, {wrongReceiver: true}, [parent]), undefined);
    while (pending.length) pending.shift()();
    trace.push(['return']);
  } catch (error) {
    assert.equal(error, exception, `unexpected module/runtime failure: ${String(error)}`);
    trace.push(['throw']); thrown = true;
  }
  if (failure) assert.ok(thrown, `failure ${failure} was never reached`);
  return {trace, imports: [...imports], registered};
}

const defaults = {finalize: true, registration: 'function', initial: true, deferred: false,
  breakpoint: 'sm', failure: '', asyncMode: 'value', paramsMode: 'provided'};
async function compare(options) {
  assert.deepEqual(await exercise(report.source, {...defaults, ...options}),
    await exercise(reference, {...defaults, ...options}), JSON.stringify(options));
  ++scenarios;
}
for (const finalize of [false, true]) for (const registration of ['function', 'object', 'absent'])
  for (const initial of [false, true]) for (const deferred of [false, true])
    for (const breakpoint of ['sm', 'md', 'lg', 'unknown'])
      for (const paramsMode of ['provided', 'absent', 'null', 'changed'])
        await compare({finalize, registration, initial, deferred, breakpoint, paramsMode});
for (const asyncMode of ['value', 'resolved', 'deferred', 'rejected', 'thenable', 'throwing-then'])
  for (const deferred of [false, true]) await compare({asyncMode, deferred});
for (const failure of ['wrap', 'register', 'super', 'storage.create', 'path.create', 'params.pathStack:get',
  'finalize', 'storage.get', 'storage.set', 'storage.purge', 'observe', 'NavDestination.create:get',
  'NavDestination.create:call', 'Column.create:call', 'Column.padding:call', 'breakpoint.create',
  'breakpoint.value', 'Button.onClick:get', 'Button.onClick:call', 'NavDestination.onReady:call',
  'ready.pathStack:get', 'pasteboard.createData:get', 'pasteboard.createData:call',
  'pasteboard.MIMETYPE_TEXT_URI:get', 'pasteboard.getSystemPasteboard:call', 'system.setData:get',
  'system.setData:call', 'ui.context', 'ui.getPromptAction:get', 'ui.getPromptAction:call',
  'prompt.showToast:get', 'prompt.showToast:call', 'dirty', 'storage.delete',
  'SubscriberManager.Get:call', 'subscriber.delete:get', 'id', 'subscriber.delete:call', 'delete.internal'])
    for (const deferred of [false, true]) await compare({failure, deferred});
// These mutations operate only on test output, never in recovery. The oracle
// must reject loss of suspension and the captured PageA receiver, and observe
// altered resource values independently of source text resemblance.
const noAwait = report.source.replace('(await $value1)', '$value1');
assert.notEqual(noAwait, report.source);
await assert.rejects(() => exercise(noAwait, defaults));
const wrongReceiver = report.source.replace('($value3 = $local2);', '($value3 = globalThis);');
assert.notEqual(wrongReceiver, report.source);
await assert.rejects(() => exercise(wrongReceiver, defaults));
const wrongOffset = report.source.replace('dx: 0, dy: (-140)', 'dx: 0, dy: (-139)');
assert.notEqual(wrongOffset, report.source);
assert.notDeepEqual(await exercise(wrongOffset, defaults), await exercise(reference, defaults));
console.log(`PageA complete record: ${scenarios} independent paired lifecycle/render/async scenarios passed; representation/platform acceptance remains separate.`);
