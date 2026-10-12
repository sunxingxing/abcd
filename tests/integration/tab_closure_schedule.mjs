import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Paired source is test-only. Exercise registered content/builder closures;
// constructors, child-view callbacks, storage and platform rendering are outside
// this bounded host protocol.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-tab-closures-'));
async function trace(source, swap, reverse, fail, ordinal) {
  const events = [], registrations = [], contents = [], builders = [], functions = new Set(), hits = new Map();
  const failure = {}, components = new Map(), resources = new Set();
  let active = false, version = 0;
  const event = (label, args = []) => {
    if (!active) return;
    events.push([label, args]); const hit = (hits.get(label) ?? 0) + 1; hits.set(label, hit);
    if (label === fail && hit === ordinal) throw failure;
  };
  const take = (fn, name, destination) => {
    assert.equal(typeof fn, 'function'); assert.equal(fn.name, name); assert.equal(fn.length, 0);
    assert.ok(!functions.has(fn) && !Object.hasOwn(fn, 'prototype')); functions.add(fn); destination.push(fn);
  };
  const globals = {ViewPU: class {}};
  for (const label of ['Column', 'Tabs', 'TabContent']) {
    const component = new Proxy({}, {get(_object, key) {
      event(`get:${label}.${String(key)}`);
      return function(...args) {
        assert.equal(this, component); event(`call:${label}.${String(key)}`);
        if (label === 'TabContent' && key === 'create') take(args[0], '', contents);
        if (label === 'TabContent' && key === 'tabBar') {
          assert.deepEqual(Object.keys(args[0]), ['builder']);
          const descriptor = Object.getOwnPropertyDescriptor(args[0], 'builder');
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
          take(descriptor.value, 'builder', builders);
        }
      };
    }});
    globals[label] = component; components.set(component, label);
  }
  const context = vm.createContext(globals);
  class OutsideCallback { constructor() { assert.fail('child construction outside selected scope'); } }
  const exports = {CommonConstants: {}, BreakpointConstants: {}, HotPointPage: OutsideCallback, TabBarViewModel: OutsideCallback};
  const dependencies = new Map();
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(specifier.startsWith('@normalized:N&&&'));
    if (!dependencies.has(specifier)) dependencies.set(specifier, new vm.SyntheticModule(Object.keys(exports), function() {
      for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
    }, {context}));
    return dependencies.get(specifier);
  });
  await module.evaluate();
  const instance = Object.create(module.namespace.TabContentView.prototype);
  instance.observeComponentCreation2 = function(callback, type) {
    assert.equal(this, instance);
    if (active) { assert.equal(type.name, 'HotPointPage'); event('observeHot'); }
    registrations.push({callback, type: components.get(type)});
  };
  const defaultBuilder = function(resource) {
    assert.equal(this, instance); assert.ok(!resources.has(resource)); resources.add(resource);
    assert.deepEqual(Object.keys(resource), ['id', 'type', 'params', 'bundleName', 'moduleName']);
    assert.ok(Array.isArray(resource.params) && resource.params.length === 0);
    event('call:DefaultTabBuilder', [resource.id]);
  };
  const tabBuilder = function(item, index) {
    assert.equal(this, instance); event('call:TabBarBuilder', [item.id, index]);
  };
  const info = [0, 1].map(value => {
    const items = [0, 1, 2, 3].map(index => ({id: value * 10 + index}));
    const object = {get getTabList() {
      event('get:getTabList', [value]); return function() {
        assert.equal(this, object); event('call:getTabList', [value]); return items;
      };
    }};
    return object;
  });
  Object.defineProperties(instance, {
    DefaultTabBuilder: {get() { event('state:DefaultTabBuilder'); return defaultBuilder; }},
    TabBarBuilder: {get() { event('state:TabBarBuilder'); return tabBuilder; }},
    tabBarsInfo: {get() { event('state:tabBarsInfo', [version]); return info[version]; }}
  });
  instance.initialRender();
  const selected = registrations.filter(value => value.type === 'TabContent'); assert.equal(selected.length, 4);
  active = true;
  try {
    for (const {callback} of selected) callback(17, true);
    assert.equal(contents.length, 4); assert.equal(builders.length, 4);
    for (let round = 0; round < 2; ++round) {
      version = swap ? round : 0;
      for (const group of reverse ? [builders, contents] : [contents, builders])
        for (const fn of group) fn.call({alienReceiver: true});
    }
    return {events};
  } catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(value => value.entity === 42938);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
  const generated = readFileSync(join(output, record.source), 'utf8');
  const paired = stripTypeScriptTypes(readFileSync(join(root, 'src/phone/src/main/ets/view/TabContentView.ts'), 'utf8'));
  let scenarios = 0;
  const failures = [{fail: '', ordinal: 1}, ...['get:TabContent.create', 'call:TabContent.create',
    'get:TabContent.tabBar', 'call:TabContent.tabBar', 'state:DefaultTabBuilder', 'call:DefaultTabBuilder',
    'state:TabBarBuilder', 'state:tabBarsInfo', 'get:getTabList', 'call:getTabList', 'call:TabBarBuilder', 'observeHot']
    .flatMap(fail => [1, 2].map(ordinal => ({fail, ordinal})))];
  for (const swap of [false, true]) for (const reverse of [false, true]) for (const {fail, ordinal} of failures) {
    assert.deepEqual(await trace(generated, swap, reverse, fail, ordinal), await trace(paired, swap, reverse, fail, ordinal));
    ++scenarios;
  }
  console.log(`Paired TabContentView closure schedule: ${scenarios} bounded callback scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
