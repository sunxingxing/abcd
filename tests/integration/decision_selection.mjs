import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const source = execFileSync(process.argv[2], ['--decision-sources'], {encoding: 'utf8'});
const recovered = runInNewContext(source);
assert.equal(recovered.length, 12);
assert.ok(!source.includes('INCOMPLETE') && !source.includes('if (') && !source.includes('$phi'));
assert.ok(!source.includes('.call('));
const counts = [1, 200, 201, 400];
const marker = {};
const error = {};
const hostile = {[Symbol.toPrimitive]() { throw new Error('unexpected coercion'); }};
const values = [undefined, null, false, -0, NaN, 2n, Symbol('truth'), hostile, new Proxy({}, {})];
const reference = (value, count) => {
  let result;
  for (let i = 0; i < count; ++i)
    result = value.items(value.probe && value.other ? value.probe : value.other);
  return result;
};
function execute(fn, count, first, second, options = {}) {
  const trace = [];
  const hits = new Map();
  let other = second;
  const event = (name) => {
    trace.push([name]);
    const hit = (hits.get(name) ?? 0) + 1;
    hits.set(name, hit);
    if (options.throwAt === name && hit === (options.ordinal ?? 1)) throw error;
  };
  const value = {
    get items() {
      event('items');
      if (options.changeAtGetter) other = false;
      const callable = function(arg) {
        event('call'); trace.push(['receiver', this === value]); trace.push(['argument', arg]);
        return marker;
      };
      callable.call = () => { event('call-trap'); throw error; };
      return callable;
    },
    get probe() {
      event('probe');
      return options.changingProbe && hits.get('probe') % 2 === 0 ? false : first;
    },
    get other() { event('other'); return other; },
  };
  let result;
  try { result = fn(value, count); } catch (caught) { result = caught; }
  return {result, trace};
}
function compare(actual, expected) {
  assert.ok(Object.is(actual.result, expected.result));
  assert.equal(actual.trace.length, expected.trace.length);
  for (let i = 0; i < actual.trace.length; ++i) {
    assert.equal(actual.trace[i][0], expected.trace[i][0]);
    assert.equal(actual.trace[i].length, expected.trace[i].length);
    if (actual.trace[i].length === 2) assert.ok(Object.is(actual.trace[i][1], expected.trace[i][1]));
  }
}
let scenarios = 0;
for (let i = 0; i < recovered.length; ++i) {
  const count = counts[i % counts.length];
  assert.equal(recovered[i].length, 1);
  for (const first of values) for (const second of values) {
    compare(execute(recovered[i], count, first, second), execute(reference, count, first, second));
    ++scenarios;
  }
  for (const options of [
    {changingProbe: true}, {changeAtGetter: true},
    ...['items', 'probe', 'other', 'call'].flatMap(throwAt => [{throwAt}, {throwAt, ordinal: 2}]),
  ]) {
    compare(execute(recovered[i], count, hostile, hostile, options), execute(reference, count, hostile, hostile, options));
    ++scenarios;
  }
}
for (const mutated of [
  source.replaceAll(' || ', ' && '),
  source.replaceAll('value.items(', 'value.items.call(value,'),
  source.replaceAll('? value.other : value.probe', '? value.probe : value.probe'),
]) {
  assert.notEqual(mutated, source);
  const [fn] = runInNewContext(mutated); // Every mutation is valid JavaScript.
  let detected = false;
  for (const first of values) for (const second of values) {
    try { compare(execute(fn, 1, first, second), execute(reference, 1, first, second)); }
    catch { detected = true; }
  }
  assert.ok(detected);
}
console.log(`decision selection: ${scenarios} independent getter/receiver/identity/exception scenarios, 3 valid-syntax mutations`);
