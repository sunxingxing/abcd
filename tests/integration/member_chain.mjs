import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const source = execFileSync(process.argv[2], ['--member-chain-sources'], {encoding: 'utf8'});
const recovered = runInNewContext(source);
assert.equal(recovered.length, 15);
assert.ok(!source.includes('INCOMPLETE') && !source.includes('.call('));
const counts = [1, 200, 201, 400, 1];
const sentinel = {};
function reference(factory, count, shared) {
  let value = factory();
  if (shared) { value.probe(); return value; }
  for (let i = 0; i < count; ++i) value = value.probe();
  return value;
}
function execute(fn, count, shared, options = {}) {
  const trace = [], objects = Array.from({length: count + 1}, () => ({}));
  let state = 0;
  function event(kind, index) {
    trace.push([kind, index, state]);
    if (options.throwAt === kind && options.index === index) throw sentinel;
  }
  for (let i = 0; i < count; ++i) {
    Object.defineProperty(objects[i], 'probe', {configurable: true, get() {
      event('getter', i); ++state;
      const method = function() {
        event('call', i);
        assert.equal(this, objects[i]);
        ++state;
        return objects[i + 1];
      };
      Object.defineProperty(method, 'call', {get() { event('call-trap', i); throw sentinel; }});
      if (options.replaceGetter) Object.defineProperty(objects[i], 'probe', {
        value() { event('replaced', i); throw sentinel; }, configurable: true,
      });
      return method;
    }});
    Object.defineProperty(objects[i], 'items', {get() { event('wrong-member', i); throw sentinel; }});
  }
  const factory = () => { event('factory', 0); ++state; return objects[0]; };
  try {
    const result = fn(factory, count, shared);
    assert.equal(result, objects[shared ? 0 : count]);
    return {trace, outcome: 'return'};
  } catch (error) {
    // Keep unexpected failures distinct from the exact injected exception.
    return {trace, outcome: error === sentinel ? 'sentinel' : error.name};
  }
}
let scenarios = 0;
for (let i = 0; i < recovered.length; ++i) {
  const count = counts[i % counts.length], shared = i % counts.length === 4;
  assert.equal(recovered[i].length, 1);
  if (!shared) assert.ok(!recovered[i].toString().includes('$value'));
  const options = [{}, {replaceGetter: true}, {throwAt: 'factory', index: 0}];
  for (const index of new Set([0, Math.floor(count / 2), count - 1]))
    for (const throwAt of ['getter', 'call']) options.push({throwAt, index});
  for (const option of options) {
    assert.deepEqual(execute(recovered[i], count, shared, option), execute(reference, count, shared, option));
    ++scenarios;
  }
}
for (const mutated of [
  source.replace('(0, value)()', '((0, value)(), (0, value)())'),
  source.replace('.probe()', '.probe.call(null)'),
  source.replace('.probe()', '.items()'),
]) {
  assert.notEqual(mutated, source);
  const [fn] = runInNewContext(mutated); // Mutations must parse before testing behavior.
  assert.notDeepEqual(execute(fn, 1, false), execute(reference, 1, false));
}
console.log(`member chain: ${scenarios} independent order/receiver/identity/exception scenarios, 3 valid-syntax mutations`);
