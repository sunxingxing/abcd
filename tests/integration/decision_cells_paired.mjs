import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
const [tool, root] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-decision-cells-'));
async function load(source) {
  class ViewPU {}
  const context = vm.createContext({ViewPU});
  const module = new vm.SourceTextModule(source, {context});
  await module.link(() => new vm.SyntheticModule(['CommonConstants', 'BreakpointConstants', 'BreakpointType', 'HotListView'], function() {
    this.setExport('CommonConstants', {});
    this.setExport('BreakpointConstants', {BREAKPOINT_SM: 'small', BREAKPOINT_MD: 'medium'});
    this.setExport('BreakpointType', {}); this.setExport('HotListView', {});
  }, {context}));
  await module.evaluate(); return module.namespace.HotRankPage;
}
function outcome(fn) {
  try { return {thrown: false, value: fn()}; }
  catch (value) { return {thrown: true, value}; }
}
function compare(a, b) {
  assert.equal(a.thrown, b.thrown);
  if (Object.prototype.toString.call(b.value) === '[object Error]') assert.equal(a.value.name, b.value.name);
  else assert.ok(Object.is(a.value, b.value));
}
function run(C, initial, input, failAt, token, coercion) {
  const events = [], owner = Object.create(C.prototype);
  const event = (...parts) => { events.push(parts); if (events.length === failAt) throw token; };
  let state = initial;
  Object.defineProperty(owner, 'titleMarginTop', {
    get() { assert.equal(this, owner); event('get'); return state; },
    set(value) { assert.equal(this, owner); event('set', value); state = value; }
  });
  const offset = coercion ? {[Symbol.toPrimitive](hint) {
    assert.equal(this, offset); event('convert', hint); return input;
  }} : input;
  return {events, offset, state: () => state, result: outcome(() => owner.calcMarginTop(offset))};
}
let scenarios = 0;
try {
  // Only ABC enters the engine; the paired oracle is opened after generation.
  const output = join(temporary, 'output');
  execFileSync(tool, [join(root, 'MultiCommunityApplication/modules.abc'), output, '--value-source', '--language-arithmetic'], {timeout: 60_000});
  const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
  const record = manifest.records.find(record => record.entity === 48945);
  assert.equal(record.source_complete, true); assert.equal(record.legacy_writer_used, undefined);
  const source = readFileSync(join(output, record.source), 'utf8');
  assert.doesNotMatch(source, /INCOMPLETE|switch\s*\(/);
  const actual = await load(source);
  const reference = await load(stripTypeScriptTypes(readFileSync(join(root, 'MultiCommunityApplication/src/rank/src/main/ets/view/HotRankPage.ts'), 'utf8')));
  const token = Symbol('original failure');
  const margins = [-1, 0, -0, 1, 51, 52, 53, 100, NaN, Infinity, -Infinity, '', '52', null, undefined, 2n, Symbol('margin')];
  const inputs = [-2147483648, 2147483648, -100, -52, -1, -0, 0, 1, 52, 100, NaN, Infinity, -Infinity, '', '2', null, undefined, 2n, Symbol('offset')];
  for (const margin of margins) for (const input of inputs) for (const coercion of [false, true])
    for (const failAt of [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]) {
      const a = run(actual, margin, input, failAt, token, coercion), b = run(reference, margin, input, failAt, token, coercion);
      assert.deepEqual(a.events, b.events);
      if (!a.result.thrown && !b.result.thrown && a.result.value === a.offset && b.result.value === b.offset) {
        assert.equal(a.result.value, a.offset); assert.equal(b.result.value, b.offset);
      } else compare(a.result, b.result);
      assert.ok(Object.is(a.state(), b.state())); ++scenarios;
    }
  for (const value of ['small', 'medium', 'large', undefined, null, {}, Symbol('breakpoint')]) {
    const a = Object.create(actual.prototype), b = Object.create(reference.prototype);
    Object.defineProperty(a, 'currentBreakpoint', {get: () => value});
    Object.defineProperty(b, 'currentBreakpoint', {get: () => value});
    assert.equal(a.getDisplayCount(), b.getDisplayCount()); ++scenarios;
  }
} finally {rmSync(temporary, {recursive: true});}
console.log(JSON.stringify({pairedRecord: 48945, scenarios,
  scope: 'complete calcMarginTop/getDisplayCount methods in controlled host; no constructor or complete UI acceptance'}));
