import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

// The paired source is an independent test input. This exercises the selected
// nested Column callback, not ViewPU construction/storage or actual rendering.
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-comment-literal-'));
async function trace(source, fold, dark, breakpoint, replace, fail) {
  const events = [], callbacks = [], failure = {}, seen = new Set();
  let active = false, nested = false;
  const event = (label, args = []) => {
    if (!active) return;
    events.push([label, JSON.parse(JSON.stringify(args))]);
    if (label === fail) throw failure;
  };
  const globals = {ViewPU: class {}};
  const labels = new Map();
  const component = label => {
    const value = new Proxy({}, {get(_object, key) {
      event(`get:${label}.${String(key)}`);
      return function(...args) {
        assert.equal(this, value);
        event(`call:${label}.${String(key)}`, args);
        if (label === 'Column' && key === 'margin') {
          assert.deepEqual(Object.keys(args[0]), ['left', 'right', 'bottom']);
          for (const key of ['left', 'right', 'bottom']) {
            const descriptor = Object.getOwnPropertyDescriptor(args[0], key);
            assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
          }
          assert.notEqual(args[0].left, args[0].bottom);
        }
      };
    }});
    labels.set(value, label); return value;
  };
  for (const name of ['Column', 'Row', 'Text', 'Image', 'If']) globals[name] = component(name);
  for (const name of ['Color', 'HorizontalAlign', 'FlexAlign', 'FontWeight', 'TextOverflow'])
    globals[name] = new Proxy({}, {get(_object, key) { return `${name}.${String(key)}`; }});
  const context = vm.createContext(globals);
  class BreakpointType {
    constructor(...values) {
      event('construct', values);
      for (const value of values) {
        assert.ok(!seen.has(value) && !seen.has(value.params)); seen.add(value); seen.add(value.params);
        assert.deepEqual(Object.keys(value), ['id', 'type', 'params', 'bundleName', 'moduleName']);
        assert.ok(Array.isArray(value.params) && value.params.length === 0);
      }
      this.values = values;
      if (replace) context.Column = component('replacementColumn');
    }
    get getValue() {
      event('get:getValue'); const receiver = this;
      return function(key) {
        assert.equal(this, receiver); event('call:getValue', [key]);
        return receiver.values[key === 'sm' ? 0 : 1];
      };
    }
  }
  const constants = new Proxy({REVIEW_COUNT_SPACE: 8}, {get(object, key) { return object[key] ?? String(key); }});
  const modules = new Map();
  const module = new vm.SourceTextModule(source, {context});
  await module.link(specifier => {
    assert.ok(specifier.startsWith('@normalized:N&&&'));
    if (!modules.has(specifier)) modules.set(specifier, new vm.SyntheticModule(
      ['CommonConstants', 'BreakpointType', 'BreakpointConstants'], function() {
        this.setExport('CommonConstants', constants); this.setExport('BreakpointType', BreakpointType);
        this.setExport('BreakpointConstants', {BREAKPOINT_SM: 'sm'});
      }, {context}));
    return modules.get(specifier);
  });
  await module.evaluate();
  const instance = Object.create(module.namespace.CommentItemView.prototype);
  instance.commentItem = {reviewCnt: 1, name: 'name'}; instance.authorName = 'author';
  for (const [name, value] of [['isFoldHorizontal', fold], ['isDarkMode', dark], ['currentBreakpoint', breakpoint]])
    Object.defineProperty(instance, name, {get() { event(`state:${name}`); return value; }});
  instance.observeComponentCreation2 = function(callback, type) {
    assert.equal(this, instance); callbacks.push({callback, type: labels.get(type), nested});
  };
  instance.ifElseBranchUpdateFunction = function(_branch, callback) {
    assert.equal(this, instance); const previous = nested; nested = true;
    try { callback(); } finally { nested = previous; }
  };
  instance.initialRender();
  for (let i = 0; i < callbacks.length; ++i)
    if (callbacks[i].type === 'If') callbacks[i].callback(17, true);
  const selected = callbacks.filter(value => value.nested && value.type === 'Column');
  assert.equal(selected.length, 1);
  active = true;
  try { selected[0].callback(17, true); return {events}; }
  catch (error) { assert.equal(error, failure); return {events, threw: true}; }
}
try {
  const output = join(temporary, 'output');
  const run = spawnSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  const record = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8')).records.find(value => value.entity === 22807);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, false);
  const generated = readFileSync(join(output, record.source), 'utf8');
  const paired = stripTypeScriptTypes(readFileSync(join(root, 'src/detail/src/main/ets/view/CommentItemView.ts'), 'utf8'));
  let scenarios = 0;
  for (const fold of [false, true, 0, undefined, Symbol('fold')]) for (const dark of [false, true])
    for (const breakpoint of ['sm', 'lg']) for (const replace of [false, true])
      for (const fail of ['', 'get:Column.margin', 'call:Column.margin', 'state:isFoldHorizontal', 'construct',
        'get:getValue', 'state:currentBreakpoint', 'call:getValue', 'get:Column.padding', 'call:Column.padding']) {
        assert.deepEqual(await trace(generated, fold, dark, breakpoint, replace, fail),
          await trace(paired, fold, dark, breakpoint, replace, fail));
        ++scenarios;
      }
  console.log(`Paired CommentItemView mixed literal: ${scenarios} bounded callback scenarios passed`);
} finally { rmSync(temporary, {recursive: true, force: true}); }
