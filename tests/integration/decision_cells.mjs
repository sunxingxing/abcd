import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8', timeout: 120_000, maxBuffer: 16 * 1024 * 1024});
assert.ok(!source.includes('INCOMPLETE'));
assert.doesNotMatch(source, /switch\s*\(|Reflect\./);
assert.ok(source.includes('||'));
const {default: functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 60);

function reference(count, object, selected, fallback) {
  const cell = object;
  for (let i = 0; i < count; ++i) if (cell.probe === true) return selected;
  return fallback;
}
function run(fn, count, hit, value, throwAt, failure, selected, fallback) {
  const events = [];
  const object = {get probe() {
    assert.equal(this, object); events.push(events.length);
    if (events.length === throwAt) throw failure;
    return events.length === hit ? true : value;
  }};
  let outcome;
  try { outcome = {returned: true, value: fn(object, selected, fallback)}; }
  catch (value) { outcome = {returned: false, value}; }
  return {events, outcome};
}
let index = 0, scenarios = 0;
for (let profile = 0; profile < 3; ++profile) for (let form = 0; form < 5; ++form)
  for (const count of [2, 200, 201, 402]) {
    const fn = functions[index++];
    const oracle = (...args) => reference(count, ...args);
    const selected = {}, fallback = {}, failure = Symbol('getter failure');
    const values = [false, 0, -0, 1, -1, NaN, Infinity, '', 'truthy', undefined, null, {}, 1n, Symbol('value')];
    for (const value of values) for (const hit of [0, 1, 2, Math.ceil(count / 2), count])
      for (const throwAt of [0, 1, 2, Math.ceil(count / 2), count, count + 1]) {
        const expected = run(oracle, count, hit, value, throwAt, failure, selected, fallback);
        const actual = run(fn, count, hit, value, throwAt, failure, selected, fallback);
        assert.deepEqual(actual.events, expected.events);
        assert.equal(actual.outcome.returned, expected.outcome.returned);
        assert.equal(actual.outcome.value, expected.outcome.value); ++scenarios;
      }
    // Property reads on null/undefined must keep the original abrupt completion.
    for (const input of [null, undefined]) {
      assert.throws(() => fn(input, selected, fallback), TypeError); ++scenarios;
    }
  }
console.log(JSON.stringify({functions: functions.length, scenarios,
  evidence: 'decoded profiles; local-cell shared decisions; copies, block splitting, reversed comparisons; getter receiver, count, order, thrown identity and result identity'}));
