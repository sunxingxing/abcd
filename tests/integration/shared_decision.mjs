import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--shared-decision'],
  {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 24);

function reference(count, selectFalse, value, selected, after, payload) {
  for (let i = 0; i < count; ++i) {
    if (selectFalse ? !(value !== i) : value === i) { selected(); break; }
  }
  after(); return payload;
}

function run(fn, value, throwAt, failure, payload) {
  const events = [];
  const callback = name => function() {
    assert.equal(this, undefined);
    events.push(name);
    if (events.length === throwAt) throw failure;
    return {ignored: name};
  };
  let outcome;
  try { outcome = {returned: true, value: fn(value, callback('selected'), callback('after'), payload)}; }
  catch (value) { outcome = {returned: false, value}; }
  return {events, outcome};
}

let index = 0, scenarios = 0, mutations = 0;
for (let profile = 0; profile < 3; ++profile) for (const count of [2, 200, 201, 400])
  for (const selectFalse of [false, true]) {
    const fn = functions[index++];
    const expectedFn = (...args) => reference(count, selectFalse, ...args);
    // Strict comparison must not coerce an object, including an exotic one.
    const coercion = {[Symbol.toPrimitive]() { throw new Error('unexpected coercion'); }};
    for (const value of [-1, 0, 1, count - 1, count, NaN, -0, '0', false, true, null,
      undefined, Symbol('input'), coercion]) {
      const payload = {value}, failure = Symbol('thrown identity');
      const expected = run(expectedFn, value, 0, failure, payload);
      for (let at = 0; at <= expected.events.length; ++at) {
        const referenceRun = run(expectedFn, value, at, failure, payload);
        const actual = run(fn, value, at, failure, payload);
        assert.deepEqual(actual.events, referenceRun.events);
        assert.equal(actual.outcome.returned, referenceRun.outcome.returned);
        assert.equal(actual.outcome.value, referenceRun.outcome.value);
        ++scenarios;
      }
    }
    const value = count - 1, payload = {}, failure = Symbol('mutation');
    const expected = run(expectedFn, value, 0, failure, payload);
    for (const duplicate of [false, true]) {
      const mutant = (value, selected, after, payload) => fn(value,
        duplicate ? function() { selected(); return selected(); } : () => undefined,
        after, payload);
      const actual = run(mutant, value, 0, failure, payload);
      assert.equal(actual.outcome.returned, true);
      assert.equal(actual.outcome.value, payload);
      assert.notDeepEqual(actual.events, expected.events);
      ++mutations;
    }
  }
// Change the complete generated source, keeping legal syntax. A reversed
// comparison must fail this behavioral oracle rather than just a parser.
const corrupted = source.replace(/ (===|!==) /g, (_, comparison) => comparison === '===' ? ' !== ' : ' === ');
assert.notEqual(corrupted, source);
const {functions: reversed} = await import(`data:text/javascript;base64,${Buffer.from(corrupted).toString('base64')}`);
for (let i = 0; i < functions.length; ++i) {
  const count = [2, 200, 201, 400][Math.floor(i / 2) % 4], selectFalse = i % 2 === 1;
  const value = -1, payload = {}, failure = Symbol('comparison');
  const expected = run((...args) => reference(count, selectFalse, ...args), value, 0, failure, payload);
  const actual = run(reversed[i], value, 0, failure, payload);
  assert.equal(actual.outcome.returned, true);
  assert.equal(actual.outcome.value, payload);
  assert.notDeepEqual(actual.events, expected.events);
  ++mutations;
}
console.log(`${process.argv[3] === '--normal' ? 'normal' : 'phased'} shared decisions: ${scenarios} independent scenarios; ${mutations} mutations refused`);
