import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Paired source enters only this independent oracle. This bounded test covers
// initialRender registration and selected native UI argument expressions, not
// an OpenHarmony UI implementation or whole-application platform acceptance.
const [tool, root] = process.argv.slice(2);
const cases = [
  [27518, 'DetailTitleView', 'detail', 'Blank'],
  [37896, 'HotPointPage', 'hot', 'Column'],
  [38925, 'HotTitleView', 'hot', 'Text']
];
const temporary = mkdtempSync(join(tmpdir(), 'abcd-ui-literal-'));
let scenarios = 0;
async function trace(source, name, target, state, device, fail) {
  const events = [], callbacks = [], failure = {}, components = new Map();
  const event = (label, args = []) => {
    events.push([label, JSON.parse(JSON.stringify(args))]);
    if (label === fail) throw failure;
  };
  const globals = {ViewPU: class {}, console};
  for (const label of ['Column', 'Blank', 'Row', 'Stack', 'Text', 'If', 'Image', '__Common__']) {
    let component;
    component = new Proxy({}, {get(_object, key) {
      event(`get:${label}.${String(key)}`);
      return function (...args) {
        assert.equal(this, component);
        event(`call:${label}.${String(key)}`, args.map(value => typeof value === 'function' ? '<callback>' : value));
      };
    }});
    globals[label] = component; components.set(component, label);
  }
  for (const label of ['FlexAlign', 'Color', 'Alignment', 'Visibility'])
    globals[label] = new Proxy({}, {get(_object, key) { return `${label}.${String(key)}`; }});
  const context = vm.createContext(globals);
  class BreakpointType {
    constructor(...values) { this.values = values; }
    getValue(value) { event('breakpoint', [value]); return this.values[0]; }
  }
  class UncalledView { constructor() { throw new Error('outside selected callback scope'); } }
  const exports = {
    CommonConstants: new Proxy({DEVICE_2IN1: '2in1'}, {get(object, key) { return object[key] ?? String(key); }}),
    BreakpointConstants: {BREAKPOINT_SM: 'sm', BREAKPOINT_MD: 'md', BREAKPOINT_LG: 'lg'},
    BreakpointType, FoundView: UncalledView, FollowView: UncalledView,
    HotTitleView: UncalledView, SearchBarView: UncalledView
  };
  const dependencies = new Map();
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(specifier === '@ohos:deviceInfo' || specifier.startsWith('@normalized:N&&&'));
    if (!dependencies.has(specifier)) {
      const values = specifier === '@ohos:deviceInfo' ? {default: {get deviceType() {
        event('device'); return device;
      }}} : exports;
      dependencies.set(specifier, new vm.SyntheticModule(Object.keys(values), function () {
        for (const [key, value] of Object.entries(values)) this.setExport(key, value);
      }, {context}));
    }
    return dependencies.get(specifier);
  });
  await module.evaluate();
  const instance = Object.create(module.namespace[name].prototype);
  instance.observeComponentCreation2 = function (callback, component) {
    assert.equal(this, instance); callbacks.push({callback, component});
  };
  for (const property of ['currentBreakpoint', 'isFoundSelected', 'isFoldHorizontal', 'isShowedButton', 'foldable'])
    Object.defineProperty(instance, property, {get() {
      event(`state:${property}`); return property === 'currentBreakpoint' ? 'sm' : state;
    }});
  instance.initialRender();
  const registered = callbacks.map(({component}) => components.get(component));
  const selected = callbacks.filter(({component}) => components.get(component) === target);
  assert.ok(selected.length);
  events.length = 0;
  try {
    for (const {callback} of selected) callback(17, true);
    return {registered, events};
  } catch (error) {
    assert.equal(error, failure);
    return {registered, events, threw: true};
  }
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8'));
  for (const [entity, name, folder, target] of cases) {
    const record = manifest.records.find(item => item.entity === entity);
    assert.equal(record.source_complete, true);
    assert.equal(record.legacy_writer_used, undefined);
    const generated = readFileSync(join(output, record.source), 'utf8');
    const paired = stripTypeScriptTypes(readFileSync(join(root, 'src', folder, 'src/main/ets/view', `${name}.ts`), 'utf8'));
    assert.ok(generated.includes(' ? '));
    // Both sides use identical bounded host protocols. Compare source behavior,
    // without treating helper behavior as proof of actual platform rendering.
    for (const state of [false, true]) for (const device of ['phone', '2in1'])
      for (const fail of ['', 'device', 'state:isFoundSelected', 'breakpoint',
        `get:${target}.height`, `call:${target}.height`, `get:${target}.padding`,
        `call:${target}.padding`, `get:${target}.fontSize`, `call:${target}.fontSize`,
        `get:${target}.opacity`, `call:${target}.opacity`]) {
        assert.deepEqual(await trace(generated, name, target, state, device, fail),
                         await trace(paired, name, target, state, device, fail), `${name}/${state}/${device}/${fail}`);
        ++scenarios;
      }
  }
  console.log(`Paired UI conditional arguments: ${scenarios} bounded callback scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
