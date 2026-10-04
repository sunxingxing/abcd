import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-minigame-optional-'));
let generated;
const output = join(temporary, 'output');
try {
  execFileSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {timeout: 60_000});
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'))).records.find(record => record.entity === 12527);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
  generated = readFileSync(join(output, record.source), 'utf8');
  assert.equal(generated.match(/\?\./g)?.length, 5);
} finally { rmSync(temporary, {recursive: true}); }
// Paired text is read only after ABC-only generation. These host protocols
// cover constructor/registered callbacks; no actual OpenHarmony UI claim.
const reference = stripTypeScriptTypes(readFileSync(join(root,
  'Immersive_src/default/src/main/ets/pages/fullScreenImmersive/componentPageImmersive/MiniGame.ts'), 'utf8'));
async function observe(source, phase, kind, mutate, failAt) {
  const events = [], callbacks = [], failure = Symbol('failure'), marker = {};
  let active = false, owner, child, reads = 0;
  const event = (...args) => { if (active) { events.push(args); if (events.length === failAt) throw failure; } };
  const payload = kind === 'object' ? {get mainWindowInfo() {event('main-info'); return marker;},
    get avoidAreaPadding() {event('padding-info'); return marker;}} : kind;
  const read = label => {
    event(label); ++reads;
    return mutate && reads > 1 ? undefined : payload;
  };
  class ViewPU {
    constructor(parent, storage, id, info) { owner = this; event('super', id); }
    createStorageLink(key, value, name) { assert.equal(this, owner); event('storage', key, name); return {get() {return read('window-util');}}; }
    finalizeConstruction() { assert.equal(this, owner); event('finalize'); }
    static create(value) { assert.equal(value, child); event('publish-child'); }
  }
  class ObservedPropertyObjectPU {
    constructor(value, receiver, name) {assert.equal(receiver, owner); event('observed', value === marker ? 'marker' : value, name);}
  }
  class Content {
    constructor(receiver, params) {
      assert.equal(receiver, owner); assert.deepEqual(Object.keys(params), ['mainWindowInfo']);
      event('construct-child', params.mainWindowInfo === marker ? 'marker' : params.mainWindowInfo); child = this;
    }
  }
  const globals = {ViewPU, ObservedPropertyObjectPU, LayoutPolicy: {matchParent: 'parent'}, ImageSize: {FILL: 'fill'}};
  const labels = new Map();
  for (const name of ['Column', 'NavDestination']) {
    let component;
    component = new Proxy({}, {get(_, key) {event(`${name}.${String(key)}:get`); return function(...args) {
      assert.equal(this, component);
      if (name === 'NavDestination' && key === 'create') args[0]();
      if (active) event(`${name}.${String(key)}`, ...args.map(value => value === marker ? 'marker' :
        typeof value === 'object' ? JSON.parse(JSON.stringify(value)) : typeof value === 'function' ? 'callback' : value));
    };}}); globals[name] = component; labels.set(component, name);
  }
  const context = vm.createContext(globals);
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => new vm.SyntheticModule(['CommonConstants', 'Content', 'Footer', 'Header'], function() {
    this.setExport('CommonConstants', {FULL_PERCENT: '100%'}); this.setExport('Content', Content);
    this.setExport('Footer', class {}); this.setExport('Header', class {});
  }, {context}));
  await module.evaluate();
  const C = module.namespace.MiniGame;
  active = phase === 'constructor';
  try {
    if (phase === 'constructor') new C({}, {});
    else {
      owner = Object.create(C.prototype);
      Object.defineProperty(owner, 'windowUtil', {get() {return read('window-util');}});
      Object.defineProperty(owner, 'mainWindowInfo', {get() {return read('window-info');}});
      owner.observeComponentCreation2 = function(callback, type) {assert.equal(this, owner); callbacks.push({callback, type: labels.get(type) ?? type.name});};
      owner.updateStateVarsOfChildByElmtId = function(id, params) {assert.equal(this, owner); event('update-child', id, params.mainWindowInfo === marker ? 'marker' : params.mainWindowInfo);};
      owner.initialRender(); callbacks.find(item => item.type === 'NavDestination').callback(17, true);
      active = true;
      const callback = callbacks.find(item => item.type === (phase === 'column' ? 'Column' : 'Content')).callback;
      callback.call({wrongReceiver: true}, 17, phase !== 'update');
      if (phase === 'content') {
        const result = child.paramsGenerator_();
        event('params-generator', result.mainWindowInfo === marker ? 'marker' : result.mainWindowInfo);
      }
    }
    return {events, threw: false};
  } catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
let scenarios = 0;
for (const phase of ['constructor', 'column', 'content', 'update'])
  for (const input of [undefined, null, false, 0, -0, NaN, '', 1n, Symbol('input'), 'object'])
    for (const mutate of [false, true]) for (const failAt of [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]) {
      assert.deepEqual(await observe(generated, phase, input, mutate, failAt), await observe(reference, phase, input, mutate, failAt), phase);
      ++scenarios;
    }
assert.equal(scenarios, 880);
console.log(JSON.stringify({pairedConstructorCallbackScenarios: scenarios}));
