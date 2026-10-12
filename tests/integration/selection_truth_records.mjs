import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';

const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-selection-truth-'));
const output = join(temporary, 'records');
let records;
try {
  // The engine finishes with ABC alone before the oracle reads paired source.
  execFileSync(tool, [join(root, 'modules.abc'), output, '--value-source'], {timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  records = new Map(manifest.records.filter(record => [25350, 48379].includes(record.entity)).map(record => {
    assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
    assert.equal(record.source_route, 'value_tree'); assert.equal(record.executable, false);
    return [record.entity, readFileSync(join(output, record.source), 'utf8')];
  }));
  assert.equal(records.size, 2);
} finally { rmSync(temporary, {recursive: true}); }

let cases = 0;
async function load(source, header = false) {
  const state = {event: undefined, pending: [], deferred: false};
  const surface = name => {
    let proxy;
    proxy = new Proxy({}, {get(_, key) {
      state.event(`${name}.${String(key)}:get`);
      return function(...args) {
        assert.equal(this, proxy); state.event(`${name}.${String(key)}:call`, args);
      };
    }});
    return proxy;
  };
  const Row = surface('Row'), Text = surface('Text');
  const context = vm.createContext({ViewPU: class {}, Row, Text, FlexAlign: {Start: 0}});
  const module = new vm.SourceTextModule(source + (header ? '\nexport {itemHeader as __header};' : ''), {context});
  const exports = ['default', 'CommonConstants', 'BreakpointType', 'DetailTitleView', 'MicroBlogView',
    'CommentListView', 'CommentBarView', 'CommentInputView', 'HotListItemView', 'HOST_LIST_ARRAY'];
  await module.link(() => new vm.SyntheticModule(exports, function() {
    for (const name of exports) this.setExport(name, name === 'CommonConstants' ? {FONT_WEIGHT_700: 700} : class {});
  }, {context}));
  await module.evaluate();
  return {state, Row, Text, method: header ? module.namespace.__header : module.namespace.DetailPage.prototype.isShow};
}

const detail = await load(records.get(25350));
const detailReference = await load(stripTypeScriptTypes(readFileSync(
  join(root, 'src/detail/src/main/ets/view/DetailPage.ts'), 'utf8')));
function show(subject, first, second, folded, fail) {
  const events = [], thrown = Symbol('failure'); let reads = 0, step = 0;
  const event = name => { events.push(name); if (++step === fail) throw thrown; };
  const receiver = {
    get currentBreakpoint() { event('breakpoint'); return reads++ ? second : first; },
    get isFoldHorizontal() { event('fold'); return folded; }
  };
  try { return {events, result: Reflect.apply(subject.method, receiver, [])}; }
  catch (error) { assert.equal(error, thrown); return {events, caught: true}; }
}
const noCoercion = {[Symbol.toPrimitive]() { assert.fail('truthiness coerced'); }};
for (const first of ['md', 'sm', 'lg', undefined, null, Symbol(), noCoercion])
  for (const second of ['sm', 'md', 'lg'])
    for (const folded of [undefined, null, false, true, -0, NaN, 0n, 1n, Symbol(), noCoercion])
      for (let fail = 0; fail <= 3; ++fail) {
        assert.deepEqual(show(detail, first, second, folded, fail), show(detailReference, first, second, folded, fail)); ++cases;
      }

const header = await load(records.get(48379), true);
const headerReference = await load(stripTypeScriptTypes(readFileSync(
  join(root, 'src/rank/src/main/ets/view/HotListView.ts'), 'utf8')), true);
function render(subject, parentKind, deferred, fail) {
  const events = [], thrown = Symbol('failure'), title = {title: 'exact captured identity'};
  let step = 0;
  subject.state.pending = []; subject.state.deferred = deferred;
  subject.state.event = (name, args) => {
    const normalized = args?.map(value => {
      if (value === title) return 'title';
      return value && typeof value === 'object' ? JSON.parse(JSON.stringify(value)) : value;
    });
    events.push([name, normalized]); if (++step === fail) throw thrown;
  };
  function owner(name) {
    let receiver;
    receiver = {get observeComponentCreation2() {
      subject.state.event(`${name}:get`);
      return function(callback, component) {
        assert.equal(this, receiver); assert.equal(callback.length, 2);
        subject.state.event(`${name}:call`, [component === subject.Row ? 'Row' : component === subject.Text ? 'Text' : assert.fail('component')]);
        const invoke = () => Reflect.apply(callback, {wrongReceiver: true}, [17, true]);
        if (deferred) subject.state.pending.push(invoke); else invoke();
      };
    }};
    return receiver;
  }
  const receiver = owner('this'), supplied = owner('parent');
  const values = [null, undefined, false, -0, NaN, '', 0n, supplied, Symbol(), 1n, 'truthy'];
  try {
    const result = Reflect.apply(subject.method, receiver, [title, values[parentKind]]);
    assert.equal(result, undefined);
    for (const callback of subject.state.pending) callback();
    events.push(['return']);
  } catch (error) {
    if (error === thrown) events.push(['caught']);
    else { assert.equal(error.name, 'TypeError'); events.push(['TypeError']); }
  }
  return events;
}
for (let parent = 0; parent < 11; ++parent) for (const deferred of [false, true])
  for (let fail = 0; fail <= 24; ++fail) {
    assert.deepEqual(render(header, parent, deferred, fail), render(headerReference, parent, deferred, fail)); ++cases;
  }
console.log(`${cases} paired truth-selection method scenarios passed; constructor/platform acceptance remains separate.`);
