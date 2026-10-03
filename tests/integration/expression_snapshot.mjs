import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['--call-snapshot-source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE') && !source.includes('.call(') && !source.includes('.apply('));
const recovered = vm.runInNewContext(source);
function reference(value) {
  const saved = value.items;
  let selected = value.probe();
  for (let index = 0; index < 2; ++index) {
    if (selected === null || selected === undefined) {selected = undefined; break;}
    selected = selected.probe();
  }
  return Reflect.apply(saved, value, [{probe: selected}]);
}
const payloads = [undefined, null, false, 0, -0, NaN, '', 'text', 1n, Symbol('value'), {}];
function observe(fn, payload, failAt, mutate, cutoff = 2) {
  const trace = [], token = {}, failure = Symbol('failure'); let steps = 0;
  const event = (...parts) => {trace.push(parts); if (++steps === failAt) throw failure;};
  const nodes = Array.from({length: 3}, () => ({}));
  const value = nodes[0];
  Object.defineProperty(value, 'items', {configurable: true, get() {
    event('items'); return function(object) {
      assert.equal(this, value); event('outer-call'); assert.deepEqual(Reflect.ownKeys(object), ['probe']); return object.probe;
    };
  }});
  for (let index = 0; index < 3; ++index) Object.defineProperty(nodes[index], 'probe', {get() {
    event('get-probe', index); const receiver = nodes[index];
    const call = function() {
      assert.equal(this, receiver); event('call-probe', index);
      if (mutate) Object.defineProperty(value, 'items', {configurable: true, value() {assert.fail('callee reread');}});
      return index === cutoff ? payload : nodes[index + 1];
    };
    Object.defineProperty(call, 'call', {get() {assert.fail('call helper');}});
    return call;
  }});
  let returned = false, result;
  try {result = fn(value); returned = true;} catch (error) {result = error;}
  if (returned) assert.ok(Object.is(result, cutoff < 2 && payload == null ? undefined : payload));
  else assert.equal(result, failure);
  return {trace, returned};
}
let cases = 0;
for (const payload of payloads) for (const mutate of [false, true]) for (let failAt = 0; failAt <= 8; ++failAt) {
  assert.deepEqual(observe(recovered, payload, failAt, mutate), observe(reference, payload, failAt, mutate)); ++cases;
}
for (const payload of [null, undefined]) for (const cutoff of [0, 1]) for (const mutate of [false, true])
  for (let failAt = 0; failAt <= 8; ++failAt) {
    assert.deepEqual(observe(recovered, payload, failAt, mutate, cutoff), observe(reference, payload, failAt, mutate, cutoff)); ++cases;
  }
console.log(`${cases} generic expression snapshots, original callee/receiver, mutation and exception-order scenarios passed`);
