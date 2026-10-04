import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const reports = execFileSync(process.argv[2], [], {encoding: 'utf8'})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 176);
const marker = {}, failure = {};
const inputs = [null, undefined, false, 0, -0, NaN, '', 'text', 1n, Symbol('input'), {}, 'function'];
const outcomes = [undefined, null, false, 0, -0, NaN, '', 1n, marker];
function reference(form, count) {
  if (form <= 4) return 'value ?? false';
  if (form === 5) return 'value ?? (0, fallback)()';
  if (form === 6) return 'value ?? fallback.field';
  if (form === 7) return '(0, value ?? false)()';
  if (form === 8) return '(value ?? false) || (0, other)()';
  if (form === 9) return 'value !== null ? value : false';
  if (form === 10) return 'value ? value : false';
  if (form === 11) return 'value !== null && value !== undefined ? other : false';
  if (form === 12) return `(() => { for (let i = 0; i < ${count}; ++i) { (0, other)(); if (value === null || value === undefined) return false; } return value; })()`;
  if (form === 13) return 'value === null ? false : value === undefined ? 0 : value';
  if (form === 14) return 'value ?? "fallback-text"';
  if (form === 15) return 'value === null ? "fallback-text" : value === undefined ? "different-text" : value';
  if (form === 16) return 'value ?? fallback';
  if (form === 17) return 'value ?? (other === null ? (0, fallback)() : false)';
  if (form === 18) return 'value === null || value === undefined ? (other === null ? false : false) : value';
  if (form === 19) return 'value === null ? 0 : value === undefined ? -0 : value';
  if (form === 20) return 'value ?? ((0, other)(), (0, fallback)())';
  if (form === 21) return '((value === null ? other === null : false) || value === null || value === undefined) ? false : value';
  assert.fail(`unknown form ${form}`);
}
function observe(source, input, output, failAt) {
  const events = [];
  const event = label => { events.push(label); if (events.length === failAt) throw failure; };
  const fn = Function(`"use strict"; return (${source});`)();
  const value = input === 'function' ? function() {
    assert.equal(this, undefined); event('value-call'); return marker;
  } : input;
  const fallback = function() { assert.equal(this, undefined); event('fallback-call'); return output; };
  Object.defineProperty(fallback, 'field', {get() { assert.equal(this, fallback); event('fallback-get'); return output; }});
  const other = function() { assert.equal(this, undefined); event('other-call'); return marker; };
  let result;
  try {
    result = fn(value, fallback, other);
    if (result === value && typeof result === 'function') result = 'value-function';
    else if (result === fallback) result = 'fallback-function';
    else if (result === other) result = 'other-function';
    else if (result === marker) result = 'marker';
  } catch (error) {
    if (error === failure) result = 'sentinel';
    else if (error instanceof TypeError) result = 'TypeError';
    else if (error instanceof ReferenceError) result = 'ReferenceError';
    else throw error;
  }
  return {events, result};
}
let scenarios = 0;
for (const report of reports) {
  assert.equal(report.source.includes('??'), report.nullish);
  const oracle = report.form === 18 || report.form === 21 ? `function(value, fallback) { return ${reference(report.form, report.count)}; let other; }` :
    `function(value, fallback, other) { return ${reference(report.form, report.count)}; }`;
  for (const input of inputs) for (const output of outcomes) for (const failAt of [0, 1, 2, 3]) {
    assert.deepEqual(observe(report.source, input, output, failAt), observe(oracle, input, output, failAt),
      `form=${report.form} count=${report.count} reversed=${report.reversed}`);
    ++scenarios;
  }
}
assert.equal(scenarios, 76032);
console.log(JSON.stringify({fixtures: reports.length, scenarios}));
