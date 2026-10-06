import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Syntax/publication regression. These scope-bearing trees test the common
// resolver/printer; actual decoded global effects are covered separately by
// literal_global_source_test. Host factories here test emitted argument flow,
// not an implementation of the pinned VM arguments capability.
const source = execFileSync(process.argv[2], ['--scope-source'], {encoding: 'utf8'});
const realm = {}, symbol = {}, alternate = {}, failure = {};
const identities = new WeakMap([[realm, 'realm'], [symbol, 'symbol'], [alternate, 'alternate']]);
let nextIdentity = 0;
function identity(input) {
  if (input === null || typeof input !== 'object' && typeof input !== 'function') return input;
  if (!identities.has(input)) identities.set(input, `input${nextIdentity++}`);
  return {identity: identities.get(input)};
}
let trace, value, missing, fail;
const context = vm.createContext({$realm: realm, $symbol: symbol,
  $snapshot(args, slots) {
    assert.equal(this, undefined);
    assert.deepEqual(Array.from(args), [value, alternate]);
    assert.deepEqual(Array.from(slots), [value]);
    trace.push('snapshot'); return [slots[0], args[1]];
  }});
const actual = vm.runInContext(source, context);
assert.equal(actual.length, 48);
const reference = ['SubscriberManager', '$realm', '$symbol', '$snapshot(arguments, [params])']
  .map(read => vm.runInContext(`(function(params) { return ${read}; })`, context));
function observe(fn, input, absent, throws) {
  trace = []; value = input; missing = absent; fail = throws;
  delete context.SubscriberManager;
  if (!missing) Object.defineProperty(context, 'SubscriberManager', {configurable: true,
    get() {trace.push('global'); if (fail) throw failure; return value;}});
  try {
    const result = fn(input, alternate);
    return {result: Array.isArray(result) ? result.map(identity) : identity(result), trace};
  }
  catch (error) {
    if (error === failure) return {failure, trace};
    assert.equal(error.name, 'ReferenceError'); return {error: error.name, trace};
  }
}
let scenarios = 0;
for (const [index, fn] of actual.entries()) {
  assert.equal(fn.length, 1);
  const expected = reference[Math.floor(index / 12)];
  for (const input of [undefined, null, false, -0, NaN, 2n, Symbol('value'), {}, alternate])
    for (const absent of [false, true]) for (const throws of [false, true]) {
      assert.deepEqual(observe(fn, input, absent, throws), observe(expected, input, absent, throws));
      ++scenarios;
    }
}
let mutations = 0;
for (const [index, from, to, expected] of [
  [0, 'return SubscriberManager', 'return (SubscriberManager, SubscriberManager)', reference[0]],
  [0, 'return SubscriberManager', '{ return SubscriberManager; let SubscriberManager; }', reference[0]],
  [12, 'return $realm', 'return $symbol', reference[1]],
  [24, 'return $symbol', 'return $realm', reference[2]],
  [36, '? params : void 0', '? void 0 : void 0', reference[3]],
]) {
  const original = actual[index].toString(); assert.ok(original.includes(from), from);
  const mutant = vm.runInContext(`(${original.replace(from, to)})`, context);
  // Parsing succeeds first; require a behavioral assertion to reject mutation.
  assert.throws(() => assert.deepEqual(observe(mutant, alternate, false, false),
    observe(expected, alternate, false, false)), error => error instanceof assert.AssertionError);
  ++mutations;
}
console.log(JSON.stringify({syntaxTrees: actual.length, scenarios, mutations, decodedFixture: false}));
