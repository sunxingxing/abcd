import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Independent paired oracle for outer constants forwarded through class methods
// and their descendants. Only selected UI callbacks run; construction, storage
// lifecycle, rendering and the actual OpenHarmony platform remain unverified.
const [tool, root] = process.argv.slice(2);
const cases = [
  ['MultiCommunityApplication', 21042, 'CommentBarView', 'src/detail/src/main/ets/view/CommentBarView.ts'],
  ['MultiCommunityApplication', 22215, 'CommentInputView', 'src/detail/src/main/ets/view/CommentInputView.ts'],
  ['push-kit-sample-code-clientdemo-arkts', 11277, 'TestAbilityPage', 'src/entry/src/main/ets/pages/TestAbilityPage.ts']
];
const temporary = mkdtempSync(join(tmpdir(), 'abcd-class-captures-'));
let scenarios = 0;
async function trace(source, name, dark, value, fail, coercionProbe = false) {
  const events = [], callbacks = [], handlers = [], components = new Map(), failure = {};
  const event = (label, args = []) => {
    events.push([label, JSON.parse(JSON.stringify(args))]);
    if (label === fail) throw failure;
  };
  let routedClass, route;
  const globals = {ViewPU: class { constructor() { routedClass = new.target; throw failure; } },
    LocalStorage: {getShared() { return {}; }}, registerNamedRoute(callback) { route = callback; }};
  for (const label of ['Column', 'Row', 'Text', 'TextArea', 'Image', 'If']) {
    let component;
    component = new Proxy({}, {get(_object, key) {
      event(`get:${label}.${String(key)}`);
      return function (...args) {
        assert.equal(this, component);
        event(`call:${label}.${String(key)}`, args.map(input => typeof input === 'function' ? '<callback>' : input));
        if (typeof args[0] === 'function') handlers.push(args[0]);
      };
    }});
    globals[label] = component; components.set(component, label);
  }
  for (const label of ['FlexAlign', 'VerticalAlign', 'Color', 'FontWeight'])
    globals[label] = new Proxy({}, {get(_object, key) { return `${label}.${String(key)}`; }});
  for (const [label, method] of [['focusControl', 'requestFocus'], ['AppStorage', 'setOrCreate']]) {
    let host;
    host = new Proxy({}, {get(_object, key) {
      assert.equal(key, method); event(`get:${label}.${method}`);
      return function (...args) { assert.equal(this, host); event(`call:${label}.${method}`, args); };
    }});
    globals[label] = host;
  }
  const context = vm.createContext(globals);
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(specifier.startsWith('@normalized:N&&&'));
    return new vm.SyntheticModule(['CommonConstants', 'BreakpointConstants'], function () {
      this.setExport('CommonConstants', {FULL_PERCENT: '100%', TEXT_AREA_MAX_LINES: 4, TEXT_AREA_MAX_LENGTH: 100});
      this.setExport('BreakpointConstants', {BREAKPOINT_LG: 'lg'});
    }, {context});
  });
  await module.evaluate();
  if (route) {
    // Obtain the genuine route class without executing its constructor body.
    assert.throws(() => route(), error => error === failure);
  }
  const instance = Object.create((routedClass ?? module.namespace[name]).prototype);
  instance.observeComponentCreation2 = function (callback, component) {
    assert.equal(this, instance); callbacks.push({callback, component});
  };
  instance.ifElseBranchUpdateFunction = function (branch, callback) {
    assert.equal(this, instance); event('branch', [branch]); callback();
  };
  let content = dark ? 'x'.repeat(100) : '';
  Object.defineProperty(instance, 'content', {get() { event('content:get'); return content; },
    set(input) { event('content:set', [input]); content = input; }});
  for (const property of ['isMouseClick', 'isDarkMode'])
    Object.defineProperty(instance, property, {get() { event(property); return property === 'isMouseClick' || dark; }});
  const parameters = {get testKey() {
    event('parameter:testKey');
    return coercionProbe ? { [Symbol.toPrimitive](hint) { event('parameter:coerce', [hint]); return hint; } } : value;
  }};
  Object.defineProperty(instance, 'want', {get() { event('want'); return {get parameters() {
    event('parameters'); return dark ? parameters : undefined;
  }}; }});
  events.length = 0;
  try {
    instance.initialRender();
    const top = callbacks.slice();
    if (name === 'CommentInputView') {
      for (const {callback} of top) callback(17, true);
    } else {
      const selected = top.filter(({component}) => components.get(component) === 'If');
      assert.equal(selected.length, 1); selected[0].callback(17, true);
      for (const {callback, component} of callbacks.slice(top.length))
        if (components.get(component) === (name === 'CommentBarView' ? 'TextArea' : 'Text')) callback(17, true);
    }
    for (const callback of handlers) callback('changed');
    return {events, registered: callbacks.map(({component}) => components.get(component)), content};
  } catch (error) {
    assert.ok(error === failure || error.name === 'TypeError');
    return {events, registered: callbacks.map(({component}) => components.get(component)), content,
      threw: error === failure ? 'injected' : error.name};
  }
}
try {
  const outputs = new Map();
  for (const [folder, entity, name, pairedPath] of cases) {
    if (!outputs.has(folder)) {
      const output = join(temporary, folder);
      const run = spawnSync(tool, [join(root, folder, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
      assert.equal(run.status, 0, run.stderr);
      outputs.set(folder, output);
    }
    const output = outputs.get(folder);
    const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(item => item.entity === entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    const generated = readFileSync(join(output, record.source), 'utf8');
    const paired = stripTypeScriptTypes(readFileSync(join(root, folder, pairedPath), 'utf8'));
    for (const dark of [false, true]) for (const value of ['payload', 17, undefined, Symbol('payload')])
      for (const fail of ['', 'get:TextArea.id', 'call:TextArea.id', 'get:TextArea.onAppear',
        'call:TextArea.onAppear', 'content:set', 'get:focusControl.requestFocus', 'call:focusControl.requestFocus',
        'get:AppStorage.setOrCreate', 'call:AppStorage.setOrCreate', 'parameter:testKey', 'parameter:coerce']) {
        assert.deepEqual(await trace(generated, name, dark, value, fail), await trace(paired, name, dark, value, fail),
          `${name}/${dark}/${String(value)}/${fail}`);
        ++scenarios;
      }
    if (name === 'TestAbilityPage') {
      // Preserve the observed paired-source discrepancy, not a fake equivalence
      // result. This ABC lowers template interpolation to ADD2. Pinned runtime
      // f04900cf, runtime_stubs-inl.h RuntimeAdd2 calls ToPrimitive without a
      // preference; js_tagged_value.h defaults to NO_PREFERENCE. It therefore
      // differs from native template ToString for custom coercion objects.
      const actual = await trace(generated, name, true, undefined, '', true);
      const original = await trace(paired, name, true, undefined, '', true);
      assert.notDeepEqual(actual, original);
      assert.deepEqual(actual.events.filter(([label]) => label === 'parameter:coerce'), [['parameter:coerce', ['default']]]);
      assert.deepEqual(original.events.filter(([label]) => label === 'parameter:coerce'), [['parameter:coerce', ['string']]]);
      assert.ok(actual.events.some(([label, args]) => label === 'call:Text.create' && args[0] === 'testKey: default'));
      assert.ok(original.events.some(([label, args]) => label === 'call:Text.create' && args[0] === 'testKey: string'));
      console.log('TestAbilityPage: paired template/ABC ADD2 coercion divergence retained; full paired equivalence is unproven');
    }
  }
  console.log(`Paired class-root captures: ${scenarios} bounded callback scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
