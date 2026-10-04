import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const reports = execFileSync(process.argv[2], [], {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024})
  .trim().split('\n').map(line => JSON.parse(line));
assert.equal(reports.length, 108);
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
  if (report.form === 5) {
    assert.equal(report.source, null); // Existing statement-prefix gap, no behavior credit.
    continue;
  }
  assert.equal(report.source.includes('??'), report.form <= 2 || report.form === 6 || report.form === 8);
  const generated = vm.runInNewContext(report.source);
  const oracle = value => {
    const kept = report.form === 8 ? value.probe() : value.probe;
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
assert.equal(scenarios, 150528);
console.log(JSON.stringify({generatedFixtures: 96, statementPrefixGaps: 12, scenarios}));
