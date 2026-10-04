import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const reports = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 152);
const marker = {}, failure = {};
const inputs = [null, undefined, false, 0, -0, NaN, '', 'text', 1n, Symbol('input'), {}];
function reference(form, count) {
  if (form <= 8 || form >= 16) return form === 18 ? 'value?.["not-an-identifier"]' :
      form === 5 ? 'value?.[key]' : form === 6 ? '(0, value?.field)()' :
      form === 8 ? '(value?.field).field' : 'value?.field';
  if (form === 9) return 'value !== undefined ? value.field : undefined';
  if (form === 10) return 'value !== null && value !== undefined ? key.field : undefined';
  if (form === 11) return 'value !== null && value !== undefined ? value.field : null';
  if (form === 12) return 'value ? value.field : undefined';
  if (form === 13) return 'value !== null && value !== undefined ? value.field : key()';
  if (form === 14) return 'value !== null && value !== undefined ? value.field.field : undefined';
  return `(() => { for (let i = 0; i < ${count}; ++i) { key(); if (value === null || value === undefined) return undefined; } return value.field; })()`;
}
function observe(source, input, fault, callableValue, propertyName) {
  const events = [];
  const fn = Function(`"use strict"; return (${source});`)();
  const value = typeof input === 'object' && input !== null ? {} : input;
  const callable = function() {
    assert.equal(this, undefined); events.push('call');
    if (fault === 'call') throw failure;
    return marker;
  };
  const getter = function() {
    assert.equal(this, value); events.push('get');
    if (fault === 'get') throw failure;
    return callableValue ? callable : marker;
  };
  const prototype = value !== null && value !== undefined && typeof value !== 'object' ? Object.getPrototypeOf(value) : undefined;
  const previous = prototype && Object.getOwnPropertyDescriptor(prototype, propertyName);
  if (prototype) Object.defineProperty(prototype, propertyName, {get: getter, configurable: true});
  if (typeof value === 'object' && value !== null) Object.defineProperty(value, propertyName, {get: getter});
  const key = Object.assign(function() {
    events.push('guard-or-fallback'); if (fault === 'call') throw failure; return marker;
  }, {
    [Symbol.toPrimitive](hint) {
      assert.equal(hint, 'string'); events.push('key');
      if (fault === 'key') throw failure;
      return 'field';
    },
  });
  Object.defineProperty(key, 'field', {get() { events.push('distinct-get'); if (fault === 'get') throw failure; return marker; }});
  let outcome;
  try {
    const result = fn(value, key);
    outcome = result === marker ? 'marker' : result === callable ? 'callable' : result;
  } catch (error) {
    if (error === failure) outcome = 'sentinel';
    else if (error instanceof TypeError) outcome = 'TypeError';
    else throw error;
  } finally {
    if (prototype) {
      if (previous) Object.defineProperty(prototype, propertyName, previous);
      else delete prototype[propertyName];
    }
  }
  return {events, outcome};
}
let scenarios = 0;
for (const report of reports) {
  if (report.optional) assert.match(report.source, /\?\./);
  else assert.doesNotMatch(report.source, /\?\./);
  const oracle = `function(value, key) { return ${reference(report.form, report.count)}; }`;
  const propertyName = report.form === 18 ? 'not-an-identifier' : 'field';
  for (const input of inputs)
    for (const fault of ['', 'key', 'get', 'call'])
      for (const callable of [false, true]) {
        assert.deepEqual(observe(report.source, input, fault, callable, propertyName), observe(oracle, input, fault, callable, propertyName),
          `form=${report.form} count=${report.count} fault=${fault}`);
        ++scenarios;
      }
}
assert.equal(scenarios, 13376);
console.log(JSON.stringify({fixtures: reports.length, scenarios}));
