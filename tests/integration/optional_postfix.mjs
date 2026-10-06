import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {isDeepStrictEqual, types} from 'node:util';

const rows = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8', maxBuffer: 8 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
const decoded = process.argv[3] === '--decoded';
assert.equal(rows.length, decoded ? 960 : 203);
const compile = text => Function(`"use strict"; return (${text});`)();
function reference(row) {
  const base = row.formed ? '(value = source())' : 'value';
  const argumentsText = row.arguments === false ? '' : 'key()';
  let text = row.firstCall ? base + `?.(${argumentsText})` : base;
  for (let i = 0; i < row.depth; ++i) {
    text += i === 0 && !row.firstCall && !row.detached ? row.computed ? '?.[key]' : '?.field' : row.computed ? '[key]' : '.field';
    if (i + 1 < row.depth || row.tailCall) {
      if (row.detached) text = `(0, ${text})`;
      text += `(${argumentsText})`;
      if (row.repeated) text += `(${argumentsText})`;
    }
  }
  if (row.detached) text = `value === null || value === undefined ? undefined : ${text}`;
  return compile(`function(value, key, source) {return ${text};}`);
}
function observe(fn, row, kind, middle, failAt, failure) {
  const events = [], marker = {}, nodes = Array.from({length: row.depth}, () => ({}));
  let sourceReads = 0;
  const event = (...args) => {events.push(args); if (events.length === failAt) throw failure;};
  for (let i = 0; i < nodes.length; ++i) Object.defineProperty(nodes[i], 'field', {get() {
    assert.equal(this, nodes[i]); event('get', i);
    Object.defineProperty(this, 'field', {get() {assert.fail('getter replayed');}});
    const result = () => i === Math.floor((row.depth - 1) / 2) && middle !== 'object' ? middle
      : i + 1 < row.depth ? nodes[i + 1] : marker;
    if (i + 1 === row.depth && !row.tailCall) return result();
    const method = function(argument) {
      assert.equal(this, row.detached ? undefined : nodes[i]);
      assert.equal(argument, row.arguments === false ? undefined : marker); event('call', i);
      return row.repeated ? function(argument) {assert.equal(this, undefined); assert.equal(argument, row.arguments === false ? undefined : marker); event('repeat', i); return result();} : result();
    };
    Object.defineProperty(method, 'call', {get() {event('call-adapter', i); throw failure;}});
    return method;
  }, configurable: true});
  const head = function(argument) {assert.equal(this, undefined); assert.equal(argument, row.arguments === false ? undefined : marker); event('head'); return nodes[0];};
  const value = kind === 'object' ? row.firstCall ? head : nodes[0] : kind;
  const key = Object.assign(function() {assert.equal(this, undefined); event('argument'); return marker;}, {
    [Symbol.toPrimitive](hint) {assert.equal(hint, 'string'); event('key'); return 'field';},
  });
  const source = function() {assert.equal(this, undefined); assert.equal(++sourceReads, 1); event('source'); return value;};
  try {
    const result = fn(value, key, source);
    return {events, outcome: result === marker ? 'marker' : result, sourceReads, failed: false};
  } catch (error) {
    if (error instanceof assert.AssertionError) throw error;
    if (Object.is(error, failure)) return {events, outcome: 'original-throw', sourceReads, failed: true};
    assert.ok(types.isNativeError(error)); assert.equal(error.name, 'TypeError');
    return {events, outcome: 'TypeError', sourceReads, failed: false};
  }
}
let cases = 0, mutations = 0;
for (const row of rows) {
  if (row.eval) {
    assert.equal(Object.hasOwn(globalThis, '__abcdOptionalEvalArgument'), false);
    const generated = compile(row.source);
    const argument = () => 'typeof __abcdOptionalEvalArgument';
    assert.equal(generated(eval, argument), 'undefined'); ++cases;
    for (const value of [null, undefined]) {
      assert.equal(generated(value, () => assert.fail('nil eval argument evaluated')), undefined); ++cases;
    }
    const wrong = compile(row.source.replace('value?.(', 'eval('));
    assert.equal(wrong(eval, argument), 'function'); ++mutations;
    continue;
  }
  assert.equal((row.source.match(/\?\./g) ?? []).length, row.detached ? 0 : 1);
  const generated = compile(row.source), oracle = reference(row);
  for (const kind of [undefined, null, false, 0, -0, NaN, '', 1n, Symbol('value'), 'object'])
    for (const middle of ['object', undefined, null, false, 0]) {
      const baseline = observe(oracle, row, kind, middle, 0, {});
      for (const failAt of new Set([0, 1, Math.ceil(baseline.events.length / 2), baseline.events.length]))
        for (const failure of [undefined, null, 0, Symbol('failure'), {}]) {
          const actual = observe(generated, row, kind, middle, failAt, failure);
          assert.ok(isDeepStrictEqual(actual, observe(oracle, row, kind, middle, failAt, failure)),
            JSON.stringify({row, kind: typeof kind, middle: typeof middle, failAt}));
          if (failAt > 0 && failAt <= baseline.events.length) assert.equal(actual.failed, true);
          ++cases;
        }
    }
  if (!decoded && !row.repeated && row.depth === 3 && !row.computed && !row.formed && !row.reversed) {
    const optional = row.source.replace(/(?<!\?)\.field/g, '?.field'); assert.notEqual(optional, row.source);
    assert.ok(!isDeepStrictEqual(observe(compile(optional), row, 'object', null, 0, {}), observe(oracle, row, 'object', null, 0, {}))); ++mutations;
    const first = row.firstCall ? 'value?.((0, key)())' : 'value?.field((0, key)())';
    const ended = row.source.replace(first, '(' + first + ')'); assert.notEqual(ended, row.source);
    assert.ok(!isDeepStrictEqual(observe(compile(ended), row, null, 'object', 0, {}), observe(oracle, row, null, 'object', 0, {}))); ++mutations;
  }
}
assert.equal(mutations, decoded ? 0 : 9);
console.log(JSON.stringify({fixtures: rows.length, cases, mutations}));
