import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const reports = execFileSync(process.argv[2], [], {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 144);
const opaque = {[Symbol.toPrimitive]() { assert.fail('coerced nullish input'); }};
const callable = function() { assert.fail('called nullish input'); };
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', 0n, 1n, Symbol('input'), opaque, callable];
const other = {}, failure = Symbol('failure');
function observe(fn, payload, fallback, mutate, failAt, invoked) {
  const events = []; let step = 0;
  const event = label => { events.push(label); if (++step === failAt) throw failure; };
  let input;
  input = {get probe() {
    assert.equal(this, input); event('probe');
    if (mutate) Object.defineProperty(input, 'probe', {get() { assert.fail('replayed producer'); }});
    return invoked ? function() { assert.equal(this, input); event('call'); return payload; } : payload;
  }, get fallback() { assert.equal(this, input); event('fallback'); return fallback; },
  get other() { assert.equal(this, input); event('other'); return other; }};
  try { return {events, value: fn(input), threw: false}; }
  catch (error) { assert.equal(error, failure); return {events, value: error, threw: true}; }
}
let scenarios = 0;
for (const report of reports) {
  assert.equal(report.source.includes('??'), report.form <= 2 || report.form === 6 || report.form === 8);
  const generated = vm.runInNewContext(report.source);
  const oracle = value => {
    const kept = report.form === 8 ? value.probe() : value.probe;
    if (report.form === 5 || report.form >= 9) {
      for (let i = 0; i < report.count; ++i) {
        if (kept === null) return value.fallback;
        value.other; debugger;
        if (kept === undefined) return value.fallback;
      }
      return kept;
    }
    if (report.form === 7) return kept;
    if (report.form === 3) return kept === null ? value.fallback : kept;
    if (report.form === 4) return kept === null || kept === undefined ? value.fallback : value.other;
    return kept ?? value.fallback;
  };
  for (const payload of values) for (const fallback of values) for (const mutate of [false, true])
    for (const failAt of [0, 1, 2, 3]) {
      const actual = observe(generated, payload, fallback, mutate, failAt, report.form === 8);
      const expected = observe(oracle, payload, fallback, mutate, failAt, report.form === 8);
      assert.deepEqual(actual.events, expected.events);
      assert.equal(actual.threw, expected.threw);
      assert.ok(Object.is(actual.value, expected.value), `profile=${report.profile} count=${report.count} form=${report.form}`);
      ++scenarios;
    }
}
for (const profile of new Set(reports.map(report => report.profile))) for (let form = 0; form < 12; ++form) {
  const smaller = reports.find(r => r.profile === profile && r.form === form && r.count === 201);
  assert.ok(smaller);
  const larger = reports.find(r => r.profile === profile && r.form === form && r.count === 402);
  assert.ok(larger.work / smaller.work < 2.1);
}
assert.equal(scenarios, 225792);
console.log(JSON.stringify({generatedFixtures: 144, statementPrefixGaps: 0, scenarios}));
