import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', timeout: 60_000}).trim().split('\n').map(JSON.parse);
assert.equal(rows.length, 72);
const reference = caught => (initial, step, report) => {
  let value = initial;
  try {while (value) {report(typeof value); value = step(value);}}
  catch (error) {return caught ? error : value;}
  return value;
};
const payload = Symbol('throw'), object = {}, fn = function() {}, symbol = Symbol('value');
const cases = [[false, []], [0, []], [-0, []], [NaN, []], ['', []], [undefined, []], [null, []],
  [object, [fn, symbol, 1n, 'text', 0]], [true, [1, 0]], [1n, [0n]], [symbol, [null]]];
let comparisons = 0, mutations = 0;
function observe(function_, initial, next, throwAt) {
  const events = []; let at = 0;
  const event = value => {events.push(value); if (events.length === throwAt) throw payload;};
  const value = function_(initial, function(previous) {
    assert.equal(this, undefined); assert.ok(at < next.length, 'loop must stop');
    assert.ok(Object.is(previous, at === 0 ? initial : next[at - 1]), 'transport preserves argument identity');
    event(['step', previous]); return next[at++];
  }, function(type) {assert.equal(this, undefined); event(['report', type]);});
  return {value, events};
}
function compare(actual, expected) {
  assert.ok(Object.is(actual.value, expected.value), 'transport preserves return/throw identity');
  assert.deepEqual(actual.events, expected.events);
}
for (const row of rows) {
  const original = reference(row.caught);
  const generated = Function('return (' + row.source + ')')();
  const parsed = parseJavaScript(row.source), step = parsed.body[0].expression.params[1].name;
  const pending = [parsed], loops = [], types = [], steps = [];
  while (pending.length) {
    const node = pending.pop();
    if (node.type === 'ForStatement') loops.push(node);
    if (node.type === 'UnaryExpression' && node.operator === 'typeof') types.push(node);
    if (node.type === 'CallExpression') {
      const callee = node.callee.type === 'SequenceExpression' ? node.callee.expressions.at(-1) : node.callee;
      if (callee.type === 'Identifier' && callee.name === step) steps.push(node);
    }
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) pending.push(...value.filter(item => item?.type));
      else if (value?.type) pending.push(value);
    }
  }
  assert.equal(loops.length, 1); assert.equal(types.length, 1); assert.equal(steps.length, 1);
  assert.notEqual(loops[0].test.type, 'Literal');
  assert.ok(loops[0].update, 'carried state update remains native');
  for (const [initial, next] of cases) {
    const expected = observe(original, initial, next, Infinity);
    compare(observe(generated, initial, next, Infinity), expected); ++comparisons;
    for (let throwAt = 1; throwAt <= expected.events.length; ++throwAt) {
      compare(observe(generated, initial, next, throwAt), observe(original, initial, next, throwAt)); ++comparisons;
    }
  }
  for (const [node, replacement] of [[loops[0].test, 'false'], [types[0], '"wrong type"'], [steps[0].arguments[0], '({})']]) {
    const changed = row.source.slice(0, node.start) + replacement + row.source.slice(node.end);
    parseJavaScript(changed); const bad = Function('return (' + changed + ')')();
    assert.throws(() => compare(observe(bad, object, [0], Infinity), observe(original, object, [0], Infinity)),
      {name: 'AssertionError'}); ++mutations;
  }
}
console.log(JSON.stringify({sources: rows.length, comparisons, mutations, platformExecution: false}));
