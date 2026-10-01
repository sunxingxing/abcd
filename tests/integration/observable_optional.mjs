import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--observable-optional-source'],
  {encoding: 'utf8', timeout: 60_000});
assert.equal(source.match(/debugger;/g)?.length, 1);
assert.ok(!source.includes('INCOMPLETE'));
const load = async text => (await import(`data:text/javascript;base64,${Buffer.from(`export default (${text});`).toString('base64')}`)).default;
const generated = await load(source);
const reference = value => {
  if (value === null || value === undefined) { debugger; return undefined; }
  return value.probe;
};

function run(fn, input, failure, throws) {
  const events = [], payload = {};
  const object = {
    get probe() {
      assert.equal(this, object); events.push('probe');
      if (throws) throw failure;
      return payload;
    },
    get items() { events.push('wrong-property'); return undefined; },
    [Symbol.toPrimitive]() { throw new Error('unexpected coercion'); },
  };
  let outcome;
  try { outcome = {returned: true, value: fn(input === 'object' ? object : input)}; }
  catch (value) { outcome = {returned: false, value}; }
  return {events, outcome, payload};
}

let scenarios = 0;
for (const input of [null, undefined, 0, false, '', 'object']) for (const throws of [false, true]) {
  const failure = Symbol('thrown identity');
  const expected = run(reference, input, failure, throws);
  const actual = run(generated, input, failure, throws);
  assert.deepEqual(actual.events, expected.events);
  assert.equal(actual.outcome.returned, expected.outcome.returned);
  if (input === 'object' && !throws) assert.equal(actual.outcome.value, actual.payload);
  else assert.equal(actual.outcome.value, expected.outcome.value);
  ++scenarios;
}
const corrupted = source.replaceAll('.probe', '.items');
assert.notEqual(corrupted, source);
const mutant = await load(corrupted);
const failure = Symbol('mutation');
assert.notDeepEqual(run(mutant, 'object', failure, false).events,
  run(reference, 'object', failure, false).events);
console.log(`observable optional arm: ${scenarios} independent scenarios; 1 mutation refused`);
