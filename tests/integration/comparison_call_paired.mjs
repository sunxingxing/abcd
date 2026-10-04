import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, root] = process.argv.slice(2);
// Generate solely from ABC before opening the independent paired oracle.
const result = JSON.parse(execFileSync(tool, [join(root, 'modules.abc'), '32895',
  '--record-source', '--value-source', '--language-arithmetic'], {encoding: 'utf8', timeout: 60_000}));
assert.equal(result.record, 32779);
assert.equal(result.source_complete, true);
assert.equal(result.legacy_writer_used, false);
assert.doesNotMatch(result.source, /INCOMPLETE|switch\s*\(/);
const paired = stripTypeScriptTypes(readFileSync(join(root, 'src/hot/src/main/ets/view/FollowView.ts'), 'utf8'));
const normalize = value => Array.isArray(value) ? Array.from(value, normalize) :
  value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).map(key => [key, normalize(value[key])])) : value;
async function load(source) {
  const state = {event() {}, click: undefined, deep: undefined};
  const components = {};
  for (const name of ['Column', 'Row', 'Text', 'Span', 'Image', 'List', 'ListItem', 'ForEach', 'If', 'Navigation']) {
    const api = new Proxy({}, {get(target, method) {
      state.event('get:host', name, method);
      const fn = function(...args) {
        assert.equal(this, api);
        if (name === 'ListItem' && method === 'create') state.deep = args[0];
        if (name === 'Row' && method === 'onClick') state.click = args[0];
        state.event('call:host', name, method, normalize(args));
      };
      Object.defineProperty(fn, 'call', {get() {assert.fail('native member Reference required');}});
      return fn;
    }});
    components[name] = api;
  }
  class ViewPU {}
  class NavPathInfo {
    constructor(name, parameter) {state.event('construct:NavPathInfo', name, parameter); this.name = name; this.parameter = parameter;}
  }
  const enumeration = new Proxy({}, {get(target, key) {return key;}});
  const context = vm.createContext({ViewPU, NavPathInfo, ...components,
    ViewStackProcessor: {StartGetAccessRecordingFor() {}, StopGetAccessRecording() {}},
    Color: enumeration, FlexAlign: enumeration, TextAlign: enumeration, HorizontalAlign: enumeration});
  const module = new vm.SourceTextModule(source, {context});
  const exports = {CommonConstants: enumeration, BreakpointConstants: enumeration,
    BreakpointType: class {}, CardItemView: class {}, TITLE_DETAIL_LIST: [], FOLLOW_LIST: []};
  await module.link(() => new vm.SyntheticModule(Object.keys(exports), function() {
    for (const [name, value] of Object.entries(exports)) this.setExport(name, value);
  }, {context}));
  await module.evaluate();
  return {C: module.namespace.FollowView, state, components};
}
function callbacks(loaded, index, item) {
  const {C, state, components} = loaded;
  state.event = () => {};
  const owner = Object.create(C.prototype), registered = [];
  Object.defineProperties(owner, {
    indexSelected: {value: 0, configurable: true}, indexTitleSelected: {value: 0, configurable: true},
    iconList: {value: [item]}
  });
  owner.observeComponentCreation2 = function(fn, component) {assert.equal(this, owner); registered.push({fn, component});};
  owner.forEachUpdateFunction = function(id, values, generator) {assert.equal(this, owner); generator(item, index);};
  owner.initialRender();
  const lists = registered.filter(entry => entry.component === components.ForEach);
  assert.equal(lists.length, 2);
  registered.length = 0;
  lists[1].fn(1, false);
  const itemCreation = registered.find(entry => entry.component === components.ListItem);
  assert.ok(itemCreation);
  itemCreation.fn(1, false);
  assert.equal(typeof state.deep, 'function');
  registered.length = 0;
  state.deep(1, false);
  const row = registered.find(entry => entry.component === components.Row);
  assert.ok(row);
  row.fn(1, false);
  const text = registered.filter(entry => entry.component === components.Text);
  assert.equal(text.length, 2);
  assert.equal(typeof state.click, 'function');
  return {owner, click: state.click, content: text[1].fn};
}
const token = Symbol('original exception'), content = Symbol('content');
function outcome(action, events, state) {
  try {return {events, returned: true, value: action(), state: state()};}
  catch (error) {
    if (error !== token) assert.equal(error.name, 'TypeError');
    return {events, returned: false, error: error === token ? token : 'TypeError', state: state()};
  }
}
function runClick(loaded, index, reads, mutate, failAt) {
  const {owner, click} = callbacks(loaded, index, {}), events = [];
  const event = (...parts) => {events.push(parts); if (events.length === failAt) throw token;};
  let selected = 0, title = 0, version = 0, read = 0;
  const scroller = {get scrollToIndex() {
    assert.equal(this, scroller); event('get:scrollToIndex'); const captured = version;
    const fn = function(value) {assert.equal(this, scroller); event('call:scrollToIndex', captured, value);};
    Object.defineProperty(fn, 'call', {get() {assert.fail('native member Reference required');}});
    return fn;
  }};
  const page = {
    clear() {assert.equal(this, page); event('call:clear');},
    pushPath(value, replace) {assert.equal(this, page); event('call:pushPath', normalize(value), replace);}
  };
  Object.defineProperties(owner, {
    indexSelected: {get() {event('get:selected'); return selected;}, set(value) {event('set:selected', value); selected = value;}},
    indexTitleSelected: {get() {
      event('get:title'); if (mutate) version ^= 1;
      return reads ? reads[read++ % reads.length] : title;
    }, set(value) {event('set:title', value); title = value;}},
    listTitleScroller: {get() {assert.equal(this, owner); event('get:scroller'); return scroller;}},
    pageInfo: {get() {assert.equal(this, owner); event('get:page'); return page;}}
  });
  loaded.state.event = event;
  try {return outcome(click, events, () => ({selected, title}));}
  finally {loaded.state.event = () => {};}
}
function runContent(loaded, kind, failAt) {
  const events = [];
  const event = (...parts) => {events.push(parts); if (events.length === failAt) throw token;};
  const item = kind === 'getter' ? {get content() {assert.equal(this, item); event('get:content'); return content;}} : kind;
  const callback = callbacks(loaded, 1, item).content;
  loaded.state.event = event;
  try {return outcome(() => callback(1, false), events, () => undefined);}
  finally {loaded.state.event = () => {};}
}
const actual = await load(result.source), reference = await load(paired);
let scenarios = 0;
for (const index of [-2147483648, -5, -1, -0, 0, 1, 2, 200, NaN, Infinity, '2', undefined, null, true, 1n, Symbol('index')])
  for (const reads of [undefined, [-1], [2], [-1, 2], [2, -1], [NaN], ['2'], [undefined], [null], [1n], [Symbol('title')]])
    for (const mutate of [false, true]) for (let failAt = 0; failAt < 18; ++failAt) {
      assert.deepEqual(runClick(actual, index, reads, mutate, failAt), runClick(reference, index, reads, mutate, failAt));
      ++scenarios;
    }
for (const item of ['getter', undefined, null, false, -0, 2, '', 1n, Symbol('item')])
  for (let failAt = 0; failAt < 10; ++failAt) {
    assert.deepEqual(runContent(actual, item, failAt), runContent(reference, item, failAt));
    ++scenarios;
  }
console.log(JSON.stringify({pairedRecord: 32779, scenarios,
  scope: 'registered list-item click and optional-content callbacks; controlled host, callee mutation, receiver, state, getters and original exceptions; no constructor or complete UI acceptance'}));
