import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Independent paired check of the registered Tabs callback and onChange
// closure. ViewPU construction/storage, ForEach and rendering are not executed.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-home-prefix-'));
const hostile = {[Symbol.toPrimitive]() { assert.fail('strict comparisons must not coerce'); }};
async function trace(source, sequence, replace, fail, ordinal) {
  const events = [], registrations = [], handlers = [], failure = {}, labels = new Map(), hits = new Map(), objects = new Set();
  let active = false, reads = 0, generation = 0;
  const event = (label, args = []) => {
    if (!active) return;
    // Compare the plain host payload across distinct VM realms; allocation
    // identity and descriptors are checked separately below.
    events.push([label, JSON.parse(JSON.stringify(args))]); const hit = (hits.get(label) ?? 0) + 1; hits.set(label, hit);
    if (label === fail && hit === ordinal) throw failure;
  };
  const globals = {ViewPU: class {}};
  const component = (label, version = 0) => {
    const value = new Proxy({}, {get(_object, key) {
      event(`get:${String(key)}`, [label, version]);
      return function(...args) {
        assert.equal(this, value);
        event(`call:${String(key)}`, [label, version, ...args.map(value => typeof value === 'function' ? '<callback>' : value)]);
        if (key === 'create') {
          assert.deepEqual(Object.keys(args[0]), ['barPosition']);
          const descriptor = Object.getOwnPropertyDescriptor(args[0], 'barPosition');
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
        }
        if (key === 'onChange') { assert.equal(args[0].length, 1); handlers.push(args[0]); }
        for (const argument of args) if (argument && typeof argument === 'object' && Object.hasOwn(argument, 'params')) {
          assert.ok(!objects.has(argument) && !objects.has(argument.params)); objects.add(argument); objects.add(argument.params);
          assert.ok(Array.isArray(argument.params) && argument.params.length === 0);
        }
      };
    }});
    labels.set(value, label); return value;
  };
  for (const name of ['Column', 'Tabs', 'ForEach']) globals[name] = component(name);
  for (const [name, values] of [['WidthBreakpoint', {WIDTH_LG: 'lg', WIDTH_XL: 'xl'}],
    ['BarPosition', {Start: 'start', End: 'end'}], ['BarMode', {Fixed: 'fixed'}]])
    globals[name] = new Proxy(values, {get(object, key) { event(`${name}:${String(key)}`); return object[key]; }});
  const context = vm.createContext(globals);
  class OutsideCallback { constructor() { assert.fail('outside selected callback'); } }
  const exports = {CommonConstants: {get FULL_PERCENT() { event('common'); return '100%'; }},
    HomeViewModel: OutsideCallback, HomeContent: OutsideCallback};
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
  const instance = Object.create(module.namespace.Home.prototype);
  instance.observeComponentCreation2 = function(callback, type) {
    assert.equal(this, instance); registrations.push({callback, type: labels.get(type)});
  };
  instance.onActiveTabChange = function(index) { assert.equal(this, instance); event('change', [index]); };
  Object.defineProperty(instance, 'mainWindowInfo', {get() {
    event('window'); const value = sequence[reads++ % sequence.length];
    if (replace) context.Tabs = component('Tabs', ++generation);
    if (value === null || value === undefined) return value;
    return {get widthBp() { event('width'); return value; }};
  }});
  instance.initialRender();
  const selected = registrations.filter(value => value.type === 'Tabs'); assert.equal(selected.length, 1);
  active = true;
  try {
    selected[0].callback(17, true);
    assert.equal(handlers.length, 1); handlers[0].call({alien: true}, 3);
    return {events};
  } catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(value => value.entity === 23956);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
  const generated = readFileSync(join(output, record.source), 'utf8');
  assert.equal(generated.match(/\?\./g)?.length, 12);
  const paired = stripTypeScriptTypes(readFileSync(join(root, 'Immersive_src/shopping/src/main/ets/view/Home.ts'), 'utf8'));
  const sequences = [['sm'], ['lg'], ['xl'], [null], [undefined], [Symbol('breakpoint')], [hostile], ['lg', 'sm', 'xl', null, undefined]];
  const failures = [{fail: '', ordinal: 1}, ...['get:create', 'call:create', 'window', 'width',
    'WidthBreakpoint:WIDTH_LG', 'WidthBreakpoint:WIDTH_XL', 'BarPosition:Start', 'BarPosition:End',
    'get:barWidth', 'call:barWidth', 'get:barHeight', 'call:barHeight', 'common', 'BarMode:Fixed',
    'get:vertical', 'call:vertical', 'call:onChange', 'change'].flatMap(fail => [1, 2].map(ordinal => ({fail, ordinal})))];
  let scenarios = 0;
  for (const sequence of sequences) for (const replace of [false, true]) for (const {fail, ordinal} of failures) {
    assert.deepEqual(await trace(generated, sequence, replace, fail, ordinal), await trace(paired, sequence, replace, fail, ordinal));
    ++scenarios;
  }
  console.log(`Paired Home decision prefixes: ${scenarios} bounded Tabs callback scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
