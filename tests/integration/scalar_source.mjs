import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const generated = await import(pathToFileURL(process.argv[2]).href);
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
];
const unary = [
  (a,b)=>-b, (a,b)=>~b, (a,b)=>!!b, (a,b)=>!b, (a,b)=>typeof b, (a,b)=>+b,
  (a,b)=>~a, (a,b)=>+a,
];
function run(fn, a, b) {
  try { return {value: fn(a,b)}; }
  catch (error) { return {error: error.constructor}; }
}
const values = [undefined, null, false, true, 0, -0, 1, -2, 31, 32, 2147483648,
  NaN, Infinity, -Infinity, '', '2', 'bad', 0n, 2n, Symbol('value')];
assert.equal(generated.binary.length, binary.length);
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
