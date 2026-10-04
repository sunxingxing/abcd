import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-normal-literal-cells-'));
function normalize(value) {
  if (Array.isArray(value)) return Array.from(value, normalize);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).map(key => [key, normalize(value[key])]));
  return value;
}
async function load(source, name) {
  const state = {record() {}};
  const components = {};
  for (const component of ['Column', 'GridRow', 'ForEach', 'Tabs', 'TabContent', '__Common__']) {
    const api = new Proxy({}, {get(target, method) {
      return function(...args) {
        assert.equal(this, api); state.record(component, method, normalize(args));
      };
    }});
    components[component] = api;
  }
  class ViewPU {}
  class BreakpointType {
    constructor(...values) { state.record('BreakpointType', 'construct', normalize(values)); this.values = values; }
    getValue(width) { state.record('BreakpointType', 'getValue', width); return this.values[width === 'large' ? 2 : width === 'extra' ? 3 : width === 'medium' ? 1 : 0]; }
  }
  const context = vm.createContext({ViewPU, ...components,
    Color: {White: 'white'}, WidthBreakpoint: {WIDTH_LG: 'large', WIDTH_XL: 'extra'},
    FlexAlign: {End: 'end'}, BarPosition: {Start: 'start'}});
  const module = new vm.SourceTextModule(source, {context});
  const constants = {FULL_PERCENT: '100%'};
  const exports = {BreakpointType, CommonConstants: constants,
    HomeConstants: {BORDER_RADIUS_LIST: [1, 2, 3]}, SectionViewModel: class {},
    HomeButton: class {}, ProductFeature: class {}, ProductInfo: class {}, ProductPrice: class {}, ProductTitle: class {}, TitleView: class {},
    BreakpointConstants: {BREAKPOINT_SM: 'small', BREAKPOINT_MD: 'medium'},
    CommentItemView: class {}, CommentBarView: class {}, default: {}};
  await module.link(() => new vm.SyntheticModule(Object.keys(exports), function() {
    for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
  }, {context}));
  await module.evaluate();
  return {C: module.namespace[name], state};
}
function run(loaded, callback, inputs, failAt, token, padding) {
  const {C, state} = loaded;
  const events = [], owner = Object.create(C.prototype), callbacks = [];
  owner.observeComponentCreation2 = function(fn) { assert.equal(this, owner); callbacks.push(fn); };
  state.record = () => {};
  owner.initialRender();
  let reads = 0;
  const event = (...parts) => { events.push(parts); if (events.length === failAt) throw token; };
  Object.defineProperty(owner, 'mainWindowInfo', {get() {
    assert.equal(this, owner); event('get:mainWindowInfo');
    return {get widthBp() { event('get:widthBp'); return inputs[reads++ % inputs.length]; }};
  }});
  Object.defineProperty(owner, 'commentPadding', {get() {
    assert.equal(this, owner); event('get:commentPadding'); return padding;
  }});
  owner.getPadding = function() { assert.equal(this, owner); event('call:getPadding'); return {id: 73}; };
  state.record = event;
  try { callbacks[callback](1, false); return {events, thrown: false}; }
  catch (error) { assert.equal(error, token); return {events, thrown: true}; }
  finally { state.record = () => {}; }
}
let scenarios = 0;
try {
  for (const record of [
    {app: 'Immersive', entity: 21488, name: 'FlashSale', paired: 'Immersive_src/shopping/src/main/ets/view/FlashSale.ts', callbacks: [0, 2]},
    {app: 'MultiCommunityApplication', entity: 24096, name: 'CommentListView', paired: 'src/detail/src/main/ets/view/CommentListView.ts', callbacks: [1]}
  ]) {
    const output = join(temporary, record.app);
    // ABC-only generation precedes opening the paired source as the oracle.
    execFileSync(tool, [join(root, record.app, 'modules.abc'), output, '--value-source', '--language-arithmetic'], {timeout: 60_000});
    const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
    const entry = manifest.records.find(row => row.entity === record.entity);
    assert.equal(entry.source_complete, true); assert.equal(entry.legacy_writer_used, false);
    const source = readFileSync(join(output, entry.source), 'utf8');
    assert.doesNotMatch(source, /INCOMPLETE|switch\s*\(/);
    const actual = await load(source, record.name);
    const reference = await load(stripTypeScriptTypes(readFileSync(join(root, record.app, record.paired), 'utf8')), record.name);
    const token = Symbol('original exception');
    for (const callback of record.callbacks)
      for (const inputs of [['small'], ['medium'], ['large'], ['extra'], ['large', 'small'], ['small', 'extra'], [undefined], [null], [Symbol('width')]])
        for (const padding of [undefined, null, false, -0, '', {id: 91}]) for (let failAt = 0; failAt < 24; ++failAt) {
          assert.deepEqual(run(actual, callback, inputs, failAt, token, padding), run(reference, callback, inputs, failAt, token, padding));
          ++scenarios;
        }
  }
} finally { rmSync(temporary, {recursive: true}); }
console.log(JSON.stringify({pairedRecords: [21488, 24096], scenarios,
  scope: 'registered margin/gutter/padding callbacks; controlled host; getter, constructor and member-call order and original exceptions; no UI or constructor acceptance'}));
