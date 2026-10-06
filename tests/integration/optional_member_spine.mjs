import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const reports = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8', maxBuffer: 8 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 96);
function reference(row) {
  const base = row.formed ? '(value = source())' : 'value';
  const first = row.computed ? '?.[key]' : '?.field';
  const rest = (row.computed ? '[key]' : '.field').repeat(row.depth - 1);
  return `function(value, key, source) {return ${base}${first}${rest}${row.call ? '(key())' : ''};}`;
}
function observe(text, row, kind, middle, failAt, failure) {
  const fn = Function(`"use strict"; return (${text});`)();
  const events = [], marker = {}, nodes = Array.from({length: row.depth}, () => ({}));
  let sourceReads = 0;
  const event = (...args) => {events.push(args); if (events.length === failAt) throw failure;};
  const leaf = function(argument) {assert.equal(this, nodes.at(-1)); assert.equal(argument, marker); event('call'); return marker;};
  for (let i = 0; i < nodes.length; ++i) Object.defineProperty(nodes[i], 'field', {configurable: true, get() {
    assert.equal(this, nodes[i]); event('get', i);
    // A second evaluation of the same producer must be visible to the oracle.
    Object.defineProperty(this, 'field', {get() {assert.fail('member producer replayed');}});
    if (i === Math.floor((row.depth - 1) / 2) && middle !== 'object') return middle;
    return i + 1 < nodes.length ? nodes[i + 1] : row.call ? leaf : marker;
  }});
  const value = kind === 'object' ? nodes[0] : kind;
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
    assert.equal(error.name, 'TypeError'); return {events, outcome: 'TypeError', sourceReads, failed: false};
  }
}
let cases = 0, mutations = 0;
for (const row of reports) {
  assert.equal((row.source.match(/\?\./g) ?? []).length, 1);
  const oracle = reference(row);
  for (const kind of [undefined, null, false, 0, -0, NaN, '', 1n, Symbol('value'), 'object'])
    for (const middle of ['object', undefined, null, false, 0]) {
      const baseline = observe(oracle, row, kind, middle, 0, {});
      for (const failAt of new Set([0, 1, Math.ceil(baseline.events.length / 2), baseline.events.length]))
        for (const failure of [undefined, null, 0, Symbol('failure'), {}]) {
          const actual = observe(row.source, row, kind, middle, failAt, failure);
          assert.deepEqual(actual, observe(oracle, row, kind, middle, failAt, failure));
          if (failAt > 0 && failAt <= baseline.events.length) assert.equal(actual.failed, true);
          ++cases;
        }
  }
  if (row.depth === 3 && !row.computed && !row.formed && !row.reversed) {
    const extraOptional = row.source.replace('value?.field.field.field', 'value?.field.field?.field');
    assert.notEqual(extraOptional, row.source); Function(`return (${extraOptional});`);
    assert.notDeepEqual(observe(extraOptional, row, 'object', null, 0, {}), observe(oracle, row, 'object', null, 0, {}));
    ++mutations;
    const ended = row.source.replace('value?.field.field.field', '(value?.field).field.field');
    assert.notEqual(ended, row.source); Function(`return (${ended});`);
    assert.notDeepEqual(observe(ended, row, null, 'object', 0, {}), observe(oracle, row, null, 'object', 0, {}));
    ++mutations;
  }
}
assert.equal(mutations, 4);
console.log(JSON.stringify({fixtures: reports.length, cases, mutations}));
