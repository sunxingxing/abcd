import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const reports = execFileSync(process.argv[2], [], {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 96);
const values = [undefined, null, false, true, 0, -0, NaN, Infinity, -Infinity, '', 'text', 0n, 1n, Symbol('input'), 'object', 'function'];
const marker = {}, failure = Symbol('failure');
function observe(fn, inputKind, mutate, failAt, invoked) {
  const events = [];
  const event = label => { events.push(label); if (events.length === failAt) throw failure; };
  const payload = inputKind === 'object' ? {} : inputKind === 'function' ? function() { assert.fail('called payload'); } : inputKind;
  if (typeof payload === 'function' || (payload !== null && typeof payload === 'object'))
    Object.defineProperty(payload, 'field', {get() { assert.equal(this, payload); event('field'); return marker; }});
  let input;
  input = {get probe() {
    assert.equal(this, input); event('probe');
    if (mutate) Object.defineProperty(input, 'probe', {get() { assert.fail('replayed producer'); }});
    return invoked ? function() { assert.equal(this, input); event('call'); return payload; } : payload;
  }, get field() { assert.equal(this, input); event('different-field'); return marker; }};
  try { return {events, value: fn(input), threw: false}; }
  catch (error) {
    if (error === failure) return {events, value: error, threw: true};
    if (error?.name === 'TypeError') return {events, value: 'TypeError', threw: true};
    throw error;
  }
}
let scenarios = 0;
for (const report of reports) {
  assert.equal(report.source.includes('?.'), report.form <= 2 || report.form >= 5);
  const generated = vm.runInNewContext(report.source);
  const oracle = value => {
    const kept = report.form === 6 ? value.probe() : value.probe;
    if (report.form === 3) return kept === null ? undefined : kept.field;
    if (report.form === 4) return kept === null || kept === undefined ? undefined : value.field;
    return kept?.field;
  };
  for (const input of values) for (const mutate of [false, true]) for (const failAt of [0, 1, 2, 3, 4]) {
    const actual = observe(generated, input, mutate, failAt, report.form === 6);
    const expected = observe(oracle, input, mutate, failAt, report.form === 6);
    assert.deepEqual(actual.events, expected.events);
    assert.equal(actual.threw, expected.threw);
    assert.ok(Object.is(actual.value, expected.value), `profile=${report.profile} count=${report.count} form=${report.form}`);
    ++scenarios;
  }
}
assert.equal(scenarios, 15360);
console.log(JSON.stringify({generatedFixtures: reports.length, scenarios}));
