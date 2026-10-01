import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--snapshot-decision'],
  {encoding: 'utf8', timeout: 120_000, maxBuffer: 16 * 1024 * 1024});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 96);

// These bytecodes branch on typed VM predicates, not general JS truthiness.
const selected = (value, negative) => negative ? value === false || value === 0 :
  value === true || (typeof value === 'number' && value !== 0);
function reference(count, negative, caught, kind, input, take, after, payload) {
  const body = () => {
    for (let i = 0; i < count; ++i) {
      const value = kind === 'call' ? input() : input < i;
      if (selected(value, negative)) { take(); break; }
    }
    after(); return payload;
  };
  if (!caught) return body();
  try { return body(); } catch (value) { return value; }
}
function run(fn, kind, sample, count, negative, throwAt, failure, payload) {
  const events = [];
  const event = name => { events.push(name); if (events.length === throwAt) throw failure; };
  let calls = 0;
  const callback = name => function() {
    assert.equal(this, undefined); assert.equal(arguments.length, 0);
    event(name); return {ignored: name};
  };
  let input;
  if (kind === 'call') input = function() {
    assert.equal(this, undefined); assert.equal(arguments.length, 0);
    event('predicate');
    const at = calls++;
    if (sample === 'late') return at === count - 1 ? !negative : negative;
    if (sample === 'never') return negative;
    return sample;
  };
  else if (sample === 'coerce') input = {[Symbol.toPrimitive](hint) {
    assert.equal(hint, 'number'); event(`coerce:${calls++}`); return negative ? -1 : count - 2;
  }};
  else input = sample;
  let outcome;
  try { outcome = {returned: true, value: fn(input, callback('selected'), callback('after'), payload)}; }
  catch (value) { outcome = {returned: false, value}; }
  return {events, outcome};
}
function compare(actual, expected) {
  assert.deepEqual(actual.events, expected.events);
  assert.equal(actual.outcome.returned, expected.outcome.returned);
  if (expected.outcome.value instanceof TypeError) assert.ok(actual.outcome.value instanceof TypeError);
  else assert.equal(actual.outcome.value, expected.outcome.value);
}
let index = 0, scenarios = 0;
for (let profile = 0; profile < 3; ++profile) for (const kind of ['call', 'less'])
  for (const count of [2, 200, 201, 400]) for (const negative of [false, true])
    for (const caught of [false, true]) {
      const fn = functions[index++];
      const referenceFn = (...args) => reference(count, negative, caught, kind, ...args);
      const samples = kind === 'call' ? ['late', 'never', true, false, 0, -0, 1, -1,
        NaN, Infinity, '', 'truthy', undefined, null, {}, Symbol('language'), 1n] :
        [-1, -0, 0, count - 2, count, Infinity, NaN, '1', undefined, null, 1n, Symbol('less'), 'coerce'];
      for (const sample of samples) {
        const payload = {}, failure = Symbol('arbitrary throw');
        const baseline = run(referenceFn, kind, sample, count, negative, 0, failure, payload);
        const positions = new Set([0, 1, 2, baseline.events.length, Math.ceil(baseline.events.length / 2)]);
        for (const at of positions) {
          const expected = run(referenceFn, kind, sample, count, negative, at, failure, payload);
          const actual = run(fn, kind, sample, count, negative, at, failure, payload);
          compare(actual, expected); ++scenarios;
        }
      }
    }
// Mutate complete source while retaining legal syntax: duplicate a RHS call,
// suppress it, or invert the outer short-circuit decision.
const call = /\(0, \$arg0\)\(\)/;
const mutants = [
  source.replace(call, '(0, $arg0)(), (0, $arg0)()'),
  source.replace(call, 'false'),
  source.replace('))) || (($value5 =', '))) && (($value5 ='),
];
let mutations = 0;
for (const mutant of mutants) {
  assert.notEqual(mutant, source);
  const result = await import(`data:text/javascript;base64,${Buffer.from(mutant).toString('base64')}`);
  const failure = Symbol('mutation'), payload = {};
  const expected = run((...args) => reference(2, false, false, 'call', ...args), 'call', 'late', 2, false, 0, failure, payload);
  const actual = run(result.functions[0], 'call', 'late', 2, false, 0, failure, payload);
  assert.throws(() => compare(actual, expected)); ++mutations;
}
console.log(`conditional snapshots: ${scenarios} independent scenarios; ${mutations} valid-syntax mutations refused`);
