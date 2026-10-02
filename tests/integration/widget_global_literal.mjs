import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Paired oracle is test-only. Run registered callbacks in a controlled host;
// full constructor/storage lifecycle, rendering and platform are not covered.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-widget-global-'));
const paired = stripTypeScriptTypes(readFileSync(join(root, 'src/entry/src/main/ets/widget/pages/WidgetCard.ts'), 'utf8'));
async function trace(source, image, fail, replace) {
  const events = [], callbacks = [], handlers = [], components = new Map(), failure = {}, constructorStop = {};
  const event = (label, args = []) => { events.push([label, JSON.parse(JSON.stringify(args))]);
    if (label === fail) throw failure; };
  let captured, instance;
  const globals = {ViewPU: class { constructor() { captured = new.target; throw constructorStop; } },
    LocalStorage: class {}, ViewStackProcessor: {AllocateNewElmetIdForNextComponent() { return 1; },
      StartGetAccessRecordingFor() {}, StopGetAccessRecording() {}}, loadEtsCard() { assert.fail('constructor must stop first'); }};
  globals.postCardAction = function (target, options) {
    assert.equal(this, undefined); assert.equal(target, instance); event('postCardAction', [options]);
  };
  for (const label of ['Flex', 'Row', 'Text', 'Span', 'If', 'Image']) {
    let component;
    component = new Proxy({}, {get(_object, key) { event(`get:${label}.${String(key)}`);
      return function (...args) { assert.equal(this, component);
        if (label === 'Flex' && key === 'create') {
          assert.deepEqual(Object.keys(args[0]), ['direction']);
          const descriptor = Object.getOwnPropertyDescriptor(args[0], 'direction');
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
          assert.equal(descriptor.value, replace ? 'replacement' : 'column');
        }
        event(`call:${label}.${String(key)}`, args.map(value => typeof value === 'function' ? '<callback>' : value));
        if (typeof args[0] === 'function') handlers.push(args[0]);
      };
    }});
    globals[label] = component; components.set(component, label);
  }
  const context = vm.createContext(globals);
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => assert.fail('WidgetCard has no imports'));
  await assert.rejects(module.evaluate(), error => error === constructorStop);
  assert.ok(captured);
  instance = Object.create(captured.prototype);
  instance.observeComponentCreation2 = function (callback, component) {
    assert.equal(this, instance); callbacks.push({callback, component});
  };
  instance.ifElseBranchUpdateFunction = function (branch, callback) {
    assert.equal(this, instance); event('branch', [branch]); callback();
  };
  for (const [key, value] of [['formId', 'form'], ['text', 'text'], ['image', image]])
    Object.defineProperty(instance, key, {get() { event(`instance:${key}`); return value; }});
  const directions = {get Column() {
    assert.equal(this, directions); event('direction:Column');
    if (replace) {
      const replacement = new Proxy({}, {get(_object, key) { event(`get:replacementFlex.${String(key)}`);
        return function (...args) { assert.equal(this, replacement);
          event(`call:replacementFlex.${String(key)}`, args.map(value => typeof value === 'function' ? '<callback>' : value));
          if (typeof args[0] === 'function') handlers.push(args[0]);
        };
      }});
      Object.defineProperty(context, 'Flex', {value: replacement, configurable: true});
    }
    return replace ? 'replacement' : 'column';
  }};
  Object.defineProperty(context, 'FlexDirection', {get() { event('global:FlexDirection'); return directions; }});
  events.length = 0;
  try {
    instance.initialRender();
    for (let i = 0; i < callbacks.length; ++i) callbacks[i].callback(17, true);
    for (const handler of handlers) handler();
    return {events, registered: callbacks.map(({component}) => components.get(component))};
  } catch (error) {
    // Node's contextified external global getter may replace its thrown value
    // with ReferenceError during name lookup; compare the observed host trace.
    if (error !== failure && error.name !== 'TypeError' && error.name !== 'ReferenceError') throw error;
    return {events, registered: callbacks.map(({component}) => components.get(component)),
      threw: error === failure ? 'injected' : error.name};
  }
}
try {
  let scenarios = 0;
  for (const [file, entity] of [['modules.abc', 12347], ['widgets.abc', 564]]) {
    const output = join(temporary, file);
    const run = spawnSync(tool, [join(root, file), output, '--value-source'], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(item => item.entity === entity);
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
    const generated = readFileSync(join(output, record.source), 'utf8');
    for (const image of ['', 'payload', undefined, Symbol('image')]) for (const replace of [false, true])
      for (const fail of ['', 'global:FlexDirection', 'direction:Column', 'get:Flex.create', 'call:Flex.create',
        'get:Flex.padding', 'call:Flex.padding', 'get:Flex.onClick', 'call:Flex.onClick', 'instance:image', 'postCardAction']) {
        assert.deepEqual(await trace(generated, image, fail, replace), await trace(paired, image, fail, replace),
          `${file}/${String(image)}/${replace}/${fail}`);
        ++scenarios;
      }
  }
  console.log(`Paired WidgetCard globals: ${scenarios} bounded callback/effect scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
