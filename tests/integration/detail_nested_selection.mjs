import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// Independent paired oracle for two GridCol callbacks and the isShow predicate.
// ViewPU construction, storage, child views and real rendering stay outside it.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-detail-nested-'));
async function trace(source, fold, breakpoint, replace, flip, fail, show = false, ordinal = 1) {
  const events = [], callbacks = [], failure = {}, seen = new Set(), labels = new Map();
  let active = false, generation = 0, reads = 0, breakpointReads = 0;
  const hits = new Map();
  const event = (label, args = []) => {
    if (!active) return;
    events.push([label, JSON.parse(JSON.stringify(args))]);
    const hit = (hits.get(label) ?? 0) + 1; hits.set(label,hit);
    if (label === fail && hit === ordinal) throw failure;
  };
  const globals = {ViewPU: class {}};
  let prototype;
  const own = (object, keys) => {
    assert.ok(!seen.has(object)); seen.add(object);
    assert.equal(Object.getPrototypeOf(object), prototype);
    assert.deepEqual(Object.keys(object), keys);
    for (const key of keys) {
      const descriptor = Object.getOwnPropertyDescriptor(object, key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
    }
  };
  const component = (label, version = 0) => {
    const value = new Proxy({}, {get(_object, key) {
      event(`get:${String(key)}`, [label, version]);
      return function(...args) {
        assert.equal(this, value); event(`call:${String(key)}`, [label, version, ...args]);
        if (active && label === 'GridCol' && key === 'create') {
          own(args[0], ['span']); own(args[0].span, ['sm', 'md', 'lg']);
          assert.equal(args[0].span.sm, 4); assert.equal(args[0].span.lg, 12);
        }
        if (active && label === 'GridCol' && key === 'padding') {
          own(args[0], ['left', 'right']); assert.notEqual(args[0].left, args[0].right);
        }
      };
    }});
    labels.set(value, label); return value;
  };
  for (const name of ['Stack', 'Column', 'Scroll', 'GridRow', 'GridCol', 'If', '__Common__', 'SideBarContainer'])
    globals[name] = component(name);
  const context = vm.createContext(globals);
  prototype = vm.runInContext('Object.prototype', context);
  class BreakpointType {
    constructor(...values) {
      event('construct', values);
      for (const value of values) {
        own(value, ['id', 'type', 'params', 'bundleName', 'moduleName']);
        assert.ok(Array.isArray(value.params) && value.params.length === 0 && !seen.has(value.params));
        seen.add(value.params);
      }
      this.values = values;
    }
    get getValue() {
      event('get:getValue'); const receiver = this;
      return function(key) {
        assert.equal(this, receiver); event('call:getValue', [key]);
        return receiver.values[key === 'sm' ? 0 : key === 'md' ? 1 : 2];
      };
    }
  }
  class OutsideCallback { constructor() { assert.fail('child construction is outside the selected callbacks'); } }
  const exports = {BreakpointType, CommonConstants: {FULL_PERCENT: '100%'},
    DetailTitleView: OutsideCallback, MicroBlogView: OutsideCallback, CommentListView: OutsideCallback,
    CommentBarView: OutsideCallback, CommentInputView: OutsideCallback, default: OutsideCallback};
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
  const instance = Object.create(module.namespace.DetailPage.prototype);
  instance.observeComponentCreation2 = function(callback, type) {
    assert.equal(this, instance); callbacks.push({callback, type: labels.get(type)});
  };
  Object.defineProperty(instance, 'isFoldHorizontal', {get() {
    event('state:isFoldHorizontal'); ++reads;
    if (replace) context.GridCol = component('GridCol', ++generation);
    return flip && reads % 2 === 0 ? !fold : fold;
  }});
  Object.defineProperty(instance, 'currentBreakpoint', {get() {
    event('state:currentBreakpoint');
    return Array.isArray(breakpoint) ? breakpoint[breakpointReads++ % breakpoint.length] : breakpoint;
  }});
  if (show) {
    active = true;
    try { return {events,value:instance.isShow()}; }
    catch (error) { assert.equal(error,failure); return {events,threw:true}; }
  }
  instance.initialRender();
  const selected = callbacks.filter(value => value.type === 'GridCol'); assert.equal(selected.length, 2);
  active = true;
  try { for (const {callback} of selected) callback(17, true); return {events}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(value => value.entity === 25350);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
  const generated = readFileSync(join(output, record.source), 'utf8');
  const paired = stripTypeScriptTypes(readFileSync(join(root, 'src/detail/src/main/ets/view/DetailPage.ts'), 'utf8'));
  let scenarios = 0;
  for (const fold of [false, true, undefined, Symbol('fold')]) for (const breakpoint of ['sm', 'md', 'lg'])
    for (const replace of [false, true]) for (const flip of [false, true])
      for (const fail of ['', 'get:create', 'call:create', 'state:isFoldHorizontal', 'construct', 'get:getValue',
        'state:currentBreakpoint', 'call:getValue', 'get:padding', 'call:padding']) {
        assert.deepEqual(await trace(generated, fold, breakpoint, replace, flip, fail),
          await trace(paired, fold, breakpoint, replace, flip, fail));
        ++scenarios;
      }
  const hostile = {[Symbol.toPrimitive]() { assert.fail('predicate coercion'); }};
  const token = Symbol('predicate');
  let predicates = 0;
  for (const fold of [false,true,undefined,null,-0,NaN,0n,1n,token,hostile])
    for (const sequence of [['sm'],['md'],['lg'],['md','sm'],['lg','sm'],['sm','md'],[undefined],[token],[hostile]])
      for (const flip of [false,true]) for (const fail of ['', 'state:currentBreakpoint', 'state:isFoldHorizontal'])
        for (const ordinal of [1,2]) {
          assert.deepEqual(await trace(generated,fold,sequence,false,flip,fail,true,ordinal),
            await trace(paired,fold,sequence,false,flip,fail,true,ordinal));
          ++predicates;
        }
  console.log(`Paired DetailPage isShow: ${predicates} predicate/short-circuit/exception scenarios passed`);
  console.log(`Paired DetailPage nested selection: ${scenarios} bounded GridCol callback scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
