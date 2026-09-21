import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const generated = await import(pathToFileURL(process.argv[2]).href);
assert.equal(generated.firstElement([42]), 42);
assert.equal(generated.firstElement('text'), 't');
assert.equal(generated.firstElement({}), undefined);
assert.throws(() => generated.firstElement(null), TypeError);
assert.equal(generated.propertyReads.length, 2);
for (let i = 0; i < generated.propertyReads.length; ++i) {
  const read = generated.propertyReads[i];
  const state = generated.propertyStates[i];
  const symbol = Symbol('key');
  const object = {answer: 42, [symbol]: 'symbol', 0: 'zero'};
  for (const key of ['answer', symbol, -0, 0n, 'absent', undefined])
    assert.equal(read(object, key), object[key]);
  const trace = [];
  const key = {[Symbol.toPrimitive](hint) {trace.push(hint); return 'answer';}};
  const receiver = new Proxy(object, {get(target, key, actualReceiver) {
    trace.push(['get', key, actualReceiver === receiver]);
    return Reflect.get(target, key, actualReceiver);
  }});
  assert.equal(read(receiver, key), 42);
  assert.deepEqual(trace, ['string', ['get', 'answer', true]]);
  trace.length = 0;
  for (const receiver of [null, undefined]) {
    assert.throws(() => read(receiver, key), TypeError);
    assert.deepEqual(trace, []);
  }
  const error = {};
  const throwingKey = {[Symbol.toPrimitive]() {throw error;}};
  assert.deepEqual(state(object, throwingKey), [object, throwingKey, error]);
  const getter = {get answer() {assert.equal(this, getter); throw error;}};
  assert.deepEqual(state(getter, 'answer'), [getter, 'answer', error]);
  const inherited = Object.create({get answer() {return this.result;}});
  inherited.result = 19;
  assert.equal(read(inherited, 'answer'), 19);
  assert.equal(read('abc', 'length'), 3);
  assert.equal(read(12, 'missing'), undefined);
}
assert.equal(generated.linear(7), 14);
assert.equal(generated.linear('a'), 'aa');
assert.equal(generated.linear(4n), 8n);
assert.ok(Number.isNaN(generated.linear(undefined)));
assert.throws(() => generated.linear(Symbol()), TypeError);
const trace = [];
assert.equal(generated.linear({[Symbol.toPrimitive](hint) {
  trace.push(hint);
  return trace.length;
}}), 3);
assert.deepEqual(trace, ['default', 'default']);
const thrown = {};
assert.throws(() => generated.throwValue(thrown), error => error === thrown);
// Independent type dispatch mirrors the pinned runtime's tagged-value tests.
const branchOracle = value => {
  switch (typeof value) {
    case 'boolean': return [value === false, value === true];
    case 'number': return [value === 0, value !== 0];
    default: return [false, false];
  }
};
assert.equal(generated.branches.length, 6);
const hostile = new Proxy({}, {get() { throw new Error('unexpected property access'); }});
for (const value of [undefined, null, false, true, 0, -0, 1, -1, NaN,
    Infinity, -Infinity, '', '0', '1', 0n, 1n, Symbol(), {}, [], hostile]) {
  const expected = branchOracle(value);
  for (let i = 0; i < generated.branches.length; ++i)
    assert.equal(generated.branches[i](value), expected[i < 3 ? 0 : 1]);
}
const binary = [
  (a,b)=>a+b, (a,b)=>a-b, (a,b)=>a*b, (a,b)=>a/b, (a,b)=>a%b, (a,b)=>a**b,
  (a,b)=>a==b, (a,b)=>a!=b, (a,b)=>a<b, (a,b)=>a<=b, (a,b)=>a>b, (a,b)=>a>=b,
  (a,b)=>a<<b, (a,b)=>a>>>b, (a,b)=>a>>b, (a,b)=>a&b, (a,b)=>a|b, (a,b)=>a^b,
  (a,b)=>a===b, (a,b)=>a!==b,
  (a,b)=>a in b,
];
const unary = [
  (a,b)=>-b, (a,b)=>~b, (a,b)=>!!b, (a,b)=>!b, (a,b)=>typeof b, (a,b)=>+b,
  (a,b)=>~a, (a,b)=>+a,
  (a,b)=>++b, (a,b)=>--b, (a,b)=>++a, (a,b)=>--a,
];
function run(fn, a, b) {
  try { return {value: fn(a,b)}; }
  catch (error) { return {error: error.constructor}; }
}
const values = [undefined, null, false, true, 0, -0, 1, -2, 31, 32, 2147483648,
  NaN, Infinity, -Infinity, '', '2', 'bad', 0n, 2n, Symbol('value'),
  2147483647, -2147483648, Number.MAX_SAFE_INTEGER, -Number.MAX_SAFE_INTEGER];
for (const fn of [generated.increment, generated.legacyIncrement])
  for (const value of values)
    assert.deepEqual(run(fn, value), run(value => ++value, value));
for (const fn of [generated.decrement, generated.legacyDecrement])
  for (const value of values)
    assert.deepEqual(run(fn, value), run(value => --value, value));
assert.equal(generated.binary.length, binary.length);
const membership = generated.binary.at(-1);
const membershipState = generated.membershipStates[0];
const inherited = Object.create({get inherited() {throw new Error('getter must not run');}});
assert.equal(membership('inherited', inherited), true);
assert.equal(membership('missing', inherited), false);
const symbolKey = Symbol('key');
assert.equal(membership(symbolKey, {[symbolKey]: undefined}), true);
assert.equal(membership(0n, {0: null}), true);
assert.equal(membership(-0, {0: null}), true);
const membershipTrace = [];
const membershipKey = {[Symbol.toPrimitive](hint) {
  membershipTrace.push(hint);
  return 'found';
}};
const membershipObject = new Proxy({}, {has(target, key) {
  membershipTrace.push(['has', key]);
  return true;
}, get() {throw new Error('get must not run');}});
assert.equal(membership(membershipKey, membershipObject), true);
assert.deepEqual(membershipTrace, ['string', ['has', 'found']]);
membershipTrace.length = 0;
for (const receiver of [undefined, null, false, 1, 1n, 'abc', Symbol()]) {
  assert.throws(() => membership(membershipKey, receiver), TypeError);
  assert.deepEqual(membershipTrace, []);
}
const membershipError = {};
const failingKey = {[Symbol.toPrimitive]() {throw membershipError;}};
assert.deepEqual(membershipState(failingKey, inherited),
  [failingKey, inherited, membershipError]);
const failingHas = new Proxy({}, {has() {throw membershipError;}});
assert.deepEqual(membershipState('found', failingHas),
  ['found', failingHas, membershipError]);
const fixed = Object.defineProperty({}, 'fixed', {value: 1, configurable: false});
assert.throws(() => membership('fixed', new Proxy(fixed, {has() {return false;}})), TypeError);
assert.equal(generated.unary.length, unary.length);
for (const [expected, actual] of [[binary, generated.binary], [unary, generated.unary]]) {
  for (let i = 0; i < expected.length; ++i) {
    for (const a of values) for (const b of values)
      assert.deepEqual(run(actual[i], a, b), run(expected[i], a, b));
    for (const throwing of [false, true]) {
      function trial(fn) {
        const trace = [];
        const object = (name, value) => ({[Symbol.toPrimitive](hint) {
          trace.push([name, hint]);
          if (throwing) throw new TypeError('coercion');
          return value;
        }});
        return {result: run(fn, object('left', 2), object('right', 3)), trace};
      }
      assert.deepEqual(trial(actual[i]), trial(expected[i]));
    }
  }
}
assert.deepEqual(generated.special.map(fn => fn()), [undefined,null,true,false,NaN,Infinity]);
assert.equal(generated.negativeBase, -8);
const bits = new DataView(new ArrayBuffer(8));
for (const [expected, value] of generated.numbers) {
  bits.setFloat64(0, value, true);
  assert.equal(bits.getBigUint64(0, true), expected);
}
assert.ok(generated.numbers.length > 4000);
// Both encoding families publish only the accumulator. User conversion runs
// exactly once with a numeric hint; abrupt conversion leaves both slots intact.
for (let i = 0; i < generated.updates.length; ++i) {
  const modern = i < 2;
  const increment = i % 2 === 0;
  for (const primitive of [3n, '3', -0, 2147483647, -2147483648]) {
    const hints = [];
    const input = {[Symbol.toPrimitive](hint) {
      hints.push(hint);
      return primitive;
    }};
    const left = modern ? 'untouched' : input;
    const right = modern ? input : 'untouched';
    let numeric = primitive;
    const expected = increment ? ++numeric : --numeric;
    const actual = generated.updates[i](left, right);
    assert.equal(actual.length, 2);
    assert.equal(actual[0], left);
    assert.ok(Object.is(actual[1], expected));
    assert.deepEqual(hints, ['number']);
  }
  const error = {};
  const input = {[Symbol.toPrimitive]() {throw error;}};
  const left = modern ? 'unchanged' : input;
  const right = modern ? input : 'unchanged';
  assert.deepEqual(generated.updates[i](left, right), [left, right, error]);
}
