import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const reports = execFileSync(process.argv[2], [], {encoding: 'utf8', maxBuffer: 32 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 80);
const values = [undefined, null, false, 0, -0, NaN, '', 1n, Symbol('input'), 'object'];
const marker = {}, failure = Symbol('failure');
function observe(fn, kind, nextKind, seed, mutate, failAt) {
  const events = [];
  const event = label => { events.push(label); if (events.length === failAt) throw failure; };
  const make = name => {
    if (name !== 'object') return name;
    const target = {};
    Object.defineProperty(target, 'field', {get() {
      assert.equal(this, target); event('field');
      return function() { assert.equal(this, undefined); event('value-call'); return marker; };
    }});
    return target;
  };
  const payload = make(kind), other = make(nextKind);
  let value;
  value = {get probe() {
    assert.equal(this, value); event('probe');
    if (mutate) Object.defineProperty(value, 'probe', {get() { assert.fail('replayed producer'); }});
    return payload;
  }, get other() { assert.equal(this, value); event('other'); return other; },
  before() { assert.equal(this, value); event('before'); },
  key() {
    assert.equal(this, value); event('key');
    return {[Symbol.toPrimitive](hint) { assert.equal(hint, 'string'); event('key-conversion'); return 'field'; }};
  }};
  try {
    const result = fn(value, seed);
    return {events, result: result === payload ? 'payload-identity' : result === other ? 'other-identity' :
      typeof result === 'function' ? 'field-function' : result, threw: false};
  } catch (error) {
    if (error === failure) return {events, result: failure, threw: true};
    if (error?.name === 'TypeError') return {events, result: 'TypeError', threw: true};
    throw error;
  }
}
// Geometric work must remain linear in composed guards, independent of arena order.
for (let form = 0; form < 10; ++form) for (const reversed of [false, true]) {
  const small = reports.find(report => report.form === form && report.reversed === reversed && report.count === 201);
  const large = reports.find(report => report.form === form && report.reversed === reversed && report.count === 402);
  assert.ok(large.work <= small.work * 2.1);
}
let scenarios = 0;
for (const report of reports) {
  const generated = Function(`"use strict"; return (${report.source});`)();
  const oracle = function(value, seed) {
    if (report.form === 6 && seed === null) return undefined;
    if (report.form === 8 && seed === undefined) return undefined;
    if (report.form === 9 && value.other === null) return undefined;
    if (report.form === 2) value.before();
    let saved = value.probe;
    if (report.form === 3) return saved ?? false;
    if (report.form === 7) {
      if (saved === null) return undefined;
      saved = value.other;
      if (saved === undefined) return undefined;
      if (saved === null && report.count === 1) return saved.field;
    }
    if (saved === null || saved === undefined) {
      if (report.form === 5) return (0, undefined)();
      return undefined;
    }
    const result = report.form === 4 ? saved[value.key()] : saved.field;
    return report.form === 5 ? (0, result)() : result;
  };
  for (const input of values) for (const other of values) for (const seed of [undefined, null, false, 'object'])
    for (const mutate of [false, true]) for (const failAt of [0, 1, 2, 3, 4, 5]) {
      assert.deepEqual(observe(generated, input, other, seed, mutate, failAt),
        observe(oracle, input, other, seed, mutate, failAt), `form=${report.form} count=${report.count} reversed=${report.reversed}`);
      ++scenarios;
    }
}
assert.equal(scenarios, 384000);
console.log(JSON.stringify({syntaxFixtures: reports.length, scenarios}));
