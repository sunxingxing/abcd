import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8', timeout: 120_000, maxBuffer: 16 * 1024 * 1024});
assert.doesNotMatch(source, /INCOMPLETE|switch\s*\(|Reflect\./);
const {default: functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 48);

function reference(count, nested, object) {
  const local = object;
  const result = {};
  for (let i = 0; i < count; ++i) result[`field${i}`] = nested ? {probe: local.probe} : local.probe;
  return result;
}
function run(fn, value, throwAt, failure) {
  const events = [];
  const object = {get probe() {
    assert.equal(this, object); events.push(events.length);
    if (events.length === throwAt) throw failure;
    return events.length % 2 ? value : object;
  }};
  try { return {events, value: fn(object), object, returned: true}; }
  catch (error) { return {events, error, returned: false}; }
}
const hostile = new Proxy({}, {get() { assert.fail('payload must not be read'); }});
const values = [undefined, null, false, true, 0, -0, NaN, Infinity, 'text', 1n, Symbol('payload'), {}, [], () => {}, hostile];
let index = 0, scenarios = 0;
for (let profile = 0; profile < 3; ++profile) for (let form = 0; form < 4; ++form)
  for (const count of [2, 200, 201, 402]) {
    const fn = functions[index++], nested = form === 3;
    const oracle = object => reference(count, nested, object);
    for (const value of values) for (const throwAt of [0, 1, 2, Math.ceil(count / 2), count, count + 1]) {
      const failure = Symbol('getter exception');
      const expected = run(oracle, value, throwAt, failure);
      const actual = run(fn, value, throwAt, failure);
      assert.deepEqual(actual.events, expected.events);
      assert.equal(actual.returned, expected.returned);
      if (!actual.returned) assert.equal(actual.error, failure);
      else {
        const keys = Array.from({length: count}, (_, i) => `field${i}`);
        assert.deepEqual(Object.keys(actual.value), keys);
        const identities = new Set();
        for (let i = 0; i < count; ++i) {
          const descriptor = Object.getOwnPropertyDescriptor(actual.value, keys[i]);
          assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
          const field = descriptor.value;
          if (nested) {
            assert.ok(!identities.has(field)); identities.add(field);
            assert.deepEqual(Object.keys(field), ['probe']);
            const own = Object.getOwnPropertyDescriptor(field, 'probe');
            assert.ok(own.writable && own.enumerable && own.configurable);
          }
          assert.ok(Object.is(nested ? field.probe : field, i % 2 ? actual.object : value));
        }
        const fresh = run(fn, value, throwAt, failure);
        assert.notEqual(fresh.value, actual.value);
        if (nested) assert.notEqual(fresh.value.field0, actual.value.field0);
      }
      ++scenarios;
    }
    for (const input of [undefined, null]) { assert.throws(() => fn(input), TypeError); ++scenarios; }
  }
console.log(JSON.stringify({functions: functions.length, scenarios,
  evidence: 'three decoded profiles; initialized local reads in native object literals; copies, split prefix and nesting; getter order, receiver, thrown identity, descriptors and fresh object/payload identity'}));
