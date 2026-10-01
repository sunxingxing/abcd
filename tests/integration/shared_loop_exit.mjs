import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const phased = process.argv[3] === '--phased';
const source = execFileSync(process.argv[2], [phased ? '--shared-exit' : '--emit'],
  {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
const {shared} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(shared.length, 3);

function reference(condition, choose, first, second, side, after, payload) {
  while (condition()) {
    if (choose()) { if (first()) continue; }
    else { if (second()) continue; }
    side(); break;
  }
  after(); return payload;
}

function run(fn, rounds, selection, repeat, throwAt, failure, payload) {
  const events = [];
  let iteration = 0, tested = 0, calls = 0;
  const effect = (name, value) => function() {
    assert.equal(this, undefined);
    assert.ok(calls < 100, 'a wrong backedge or missing break must not spin');
    events.push(name);
    if (++calls === throwAt) throw failure;
    return value();
  };
  const callbacks = [
    effect('condition', () => ++iteration <= rounds),
    effect('choose', () => selection === 0 ? false : selection === 1 ? true : iteration % 2 === 0),
    effect('first', () => ++tested <= repeat),
    effect('second', () => ++tested <= repeat),
    effect('side', () => ({ignored: 'side'})),
    effect('after', () => ({ignored: 'after'})),
  ];
  let outcome;
  try { outcome = {returned: true, value: fn(...callbacks, payload)}; }
  catch (value) { outcome = {returned: false, value}; }
  return {events, outcome};
}

let scenarios = 0;
let mutations = 0;
for (const fn of shared) {
  for (const rounds of [0, 1, 2, 7]) for (const selection of [0, 1, 2])
    for (const repeat of [0, 1, 2, 8]) {
    const payload = {rounds, selection, repeat};
    const failure = Symbol('thrown identity');
    const expected = run(reference, rounds, selection, repeat, 0, failure, payload);
    const actual = run(fn, rounds, selection, repeat, 0, failure, payload);
    assert.deepEqual(actual.events, expected.events);
    assert.equal(actual.outcome.returned, expected.outcome.returned);
    assert.equal(actual.outcome.value, expected.outcome.value);
    ++scenarios;
    // Each observable site may throw. No subsequent tail or after-call may run.
    for (let at = 1; at <= expected.events.length; ++at) {
      const expectedThrow = run(reference, rounds, selection, repeat, at, failure, payload);
      const actualThrow = run(fn, rounds, selection, repeat, at, failure, payload);
      assert.deepEqual(actualThrow.events, expectedThrow.events);
      assert.equal(actualThrow.outcome.returned, false);
      assert.equal(actualThrow.outcome.value, failure);
      ++scenarios;
    }
  }
  const payload = {}, failure = Symbol('mutation');
  const expected = run(reference, 1, 1, 0, 0, failure, payload);
  for (const duplicate of [false, true]) {
    // Both mutants execute otherwise valid generated JS. The independent
    // oracle must observe an omitted or duplicated shared-tail effect.
    const mutant = (...args) => {
      const original = args[4];
      args[4] = duplicate ? function() { original(); return original(); } : () => undefined;
      return fn(...args);
    };
    const actual = run(mutant, 1, 1, 0, 0, failure, payload);
    assert.equal(actual.outcome.returned, true);
    assert.equal(actual.outcome.value, payload);
    assert.notDeepEqual(actual.events, expected.events);
    ++mutations;
  }
}
console.log(`${phased ? 'phased' : 'normal'} shared loop exits: ${scenarios} independent scenarios; ${mutations} mutations refused`);
