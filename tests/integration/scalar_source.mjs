import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const generated = await import(pathToFileURL(process.argv[2]).href);
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
