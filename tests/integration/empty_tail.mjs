import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import vm from 'node:vm';
const generated = vm.runInNewContext(execFileSync(process.argv[2], ['--empty-tail-sources'], { encoding: 'utf8' }));
const expected = [value => { value?.probe; }, value => value == null ? value : value.probe];
const token = {}, thrown = {};
let count = 0;
for (let i = 0; i < generated.length; ++i) {
  for (const value of [null, undefined, 0, false, '', 1n, Symbol('x')]) {
    assert.equal(generated[i](value), expected[i](value)); ++count;
  }
  for (const result of [undefined, null, false, 0, -0, NaN, '', token]) for (const fail of [false, true]) {
    const run = fn => {
      const events = [];
      const receiver = { get probe() {
        assert.equal(this, receiver); events.push('probe');
        if (fail) throw thrown;
        return result;
      }};
      try { return { events, value: fn(receiver) }; }
      catch (error) { assert.equal(error, thrown); return { events, error }; }
    };
    const actual = run(generated[i]), reference = run(expected[i]);
    assert.deepEqual(actual.events, reference.events);
    assert.equal(actual.value, reference.value);
    assert.equal(actual.error, reference.error);
    ++count;
  }
}
console.log(`Jump-only shared tails: ${count} optional access and phi identity scenarios passed`);
