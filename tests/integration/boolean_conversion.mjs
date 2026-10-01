import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const sources = execFileSync(process.argv[2], ['--boolean-sources'], {encoding: 'utf8'});
const generated = vm.runInNewContext(sources);
const reference = [value => !!value.probe, value => value.probe === null];
const poison = {[Symbol.toPrimitive]() { throw new Error('unexpected coercion'); }};
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', 0n, 1n, Symbol('value'), poison];
let scenarios = 0;
for (let mode = 0; mode < generated.length; ++mode) {
  for (const value of values) for (const fail of [false, true]) {
    const failure = {};
    const run = fn => {
      let reads = 0;
      const receiver = {get probe() {
        assert.equal(this, receiver);
        ++reads;
        if (fail) throw failure;
        return value;
      }};
      try { return {result: fn(receiver), reads}; }
      catch (error) { assert.equal(error, failure); return {error, reads}; }
    };
    assert.deepEqual(run(generated[mode]), run(reference[mode]));
    ++scenarios;
  }
}
console.log(`Boolean conversion: ${scenarios} value/getter/exception scenarios passed`);
