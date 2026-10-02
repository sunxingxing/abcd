import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

// Native conditional syntax is the independent oracle. Getter mutations test
// the reference retrieved before either branch; no mutable .call is involved.
const source = execFileSync(process.argv[2], ['--forwarded-selection-sources'], {encoding: 'utf8'});
const functions = runInNewContext(source);
const counts = [1, 2, 200, 201, 402];
assert.equal(functions.length, 3 * (counts.length + 1));
assert.ok(!source.includes('if (') && !source.includes('.call('));
const failure = {}, marker = {}, token = {};
const hostile = {[Symbol.toPrimitive]() { assert.fail('truthiness must not coerce'); }};
const values = [undefined, null, false, -0, NaN, 1, 2n, Symbol('value'), token, hostile];
const reference = (value, count) => {
  let result;
  for (let i = 0; i < count; ++i)
    result = value.items(value.probe ? (value.other ? value.probe : []) : []);
  return result;
};
const lateReference = value => {
  const argument = value.probe ? (value.other ? value.probe : []) : [];
  return value.items(argument);
};
function execute(fn, count, probe, other, options) {
  const events = [], arrays = new Set(), hits = new Map();
  let replaced = false;
  const event = name => {
    events.push(name);
    const ordinal = (hits.get(name) ?? 0) + 1; hits.set(name, ordinal);
    if (options.fail === name && ordinal === options.ordinal) throw failure;
  };
  const value = {
    get items() {
      event('items');
      const replacement = replaced;
      const fn = function(argument) {
        assert.equal(this, value);
        event(replacement ? 'replacement-call' : 'call');
        if (Array.isArray(argument)) {
          assert.equal(argument.length, 0); assert.ok(!arrays.has(argument)); arrays.add(argument);
          events.push(['array']);
        } else events.push(['value', argument]);
        return marker;
      };
      Object.defineProperty(fn, 'call', {get() { assert.fail('callee.call must not be observed'); }});
      return fn;
    },
    get probe() {
      event('probe');
      if (options.replace) replaced = true;
      return options.change && hits.get('probe') % 2 === 0 ? token : probe;
    },
    get other() { event('other'); return other; }
  };
  let result;
  try { result = fn(value, count); } catch (error) { assert.equal(error, failure); result = failure; }
  return {events, result};
}
let scenarios = 0;
for (let i = 0; i < functions.length; ++i) {
  const variant = i % (counts.length + 1), count = counts[variant] ?? 1;
  const oracle = variant === counts.length ? lateReference : reference;
  for (const probe of values) for (const other of [false, true])
    for (const options of [{}, {replace: true}, {change: true},
      ...['items', 'probe', 'other', 'call'].flatMap(fail => [1, 2].map(ordinal => ({fail, ordinal})))]) {
      assert.deepEqual(execute(functions[i], count, probe, other, options),
                       execute(oracle, count, probe, other, options));
      ++scenarios;
    }
}
console.log(`Forwarded selection: ${scenarios} independent effect/reference scenarios passed`);
