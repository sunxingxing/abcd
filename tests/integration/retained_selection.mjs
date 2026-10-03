import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const source = execFileSync(process.argv[2], ['--retained-selection-source'], {encoding: 'utf8'});
assert.ok(source.includes('debugger;') && !source.includes('INCOMPLETE'));
const recovered = vm.runInNewContext(source);
const reference = value => {
  const decision = value.probe;
  const kept = decision === false || decision === 0 ? undefined : value.other;
  debugger;
  return value.items(kept === null || kept === undefined ? 7 : kept);
};
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', 1n, Symbol('value'), {}];
function run(fn, branch, payload, failAt, mutate) {
  const trace = [], failure = Symbol('failure'); let step = 0;
  const event = (...parts) => {trace.push(parts); if (++step === failAt) throw failure;};
  const value = {get probe() {event('probe'); return branch;}, get other() {event('other'); return payload;},
    get items() {
      event('items');
      if (mutate) Object.defineProperty(value, 'other', {get() {assert.fail('retained value recomputed');}});
      return function(arg) {assert.equal(this, value); event('call', arg); return arg;};
    }};
  let returned = false, result;
  try {result = fn(value); returned = true;} catch (error) {assert.equal(error, failure);}
  return {trace, returned, result};
}
let cases = 0;
for (const branch of values) for (const payload of values) for (const mutate of [false, true])
  for (let failAt = 0; failAt <= 4; ++failAt) {
    assert.deepEqual(run(recovered, branch, payload, failAt, mutate), run(reference, branch, payload, failAt, mutate)); ++cases;
  }
console.log(`${cases} retained phi value, original branch predicate, identity, receiver and exception-order scenarios passed`);
