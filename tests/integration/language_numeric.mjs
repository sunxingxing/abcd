import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8', maxBuffer: 8 * 1024 * 1024});
const fixtures = (await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`)).default;
assert.equal(fixtures.length, 420);
// Independent source-language operators; no oracle enters the decompiler.
const numeric = value => { let scratch = value; return scratch++; };
const increment = value => { let scratch = value; return ++scratch; };
const decrement = value => { let scratch = value; return --scratch; };
// Both encodings preserve ToNumeric BigInt; ToNumber separately rejects it.
const operations = [value=>-value, increment, decrement, value=>+value, numeric,
  value=>-value, increment, decrement, numeric, value=>+value];
const token = {exception: true}, symbol = Symbol('operand');
const view = new DataView(new ArrayBuffer(8));
view.setBigUint64(0, 0xfff7000000000005n); const payloadNaN = view.getFloat64(0);
const values = [undefined, null, false, true, 0, -0, 1, -1, 2, -2, 0.5,
  -2147483648, 2147483648, 2147483647, Number.MIN_VALUE, Number.MAX_VALUE,
  Infinity, -Infinity, NaN, payloadNaN, '', '2', 'text', 0n, 2n, -1n, symbol];
let scenarios = 0;
function invoke(fn, args) {
  try { return {thrown: false, value: fn(...args)}; }
  catch (value) { return {thrown: true, value}; }
}
function compare(actual, expected, phased) {
  assert.equal(actual.thrown, phased ? false : expected.thrown);
  if (expected.value instanceof Error) {
    assert.ok(actual.value instanceof expected.value.constructor);
  } else assert.ok(Object.is(actual.value, expected.value));
}
for (const fixture of fixtures) {
  const {op, form, phased, fn} = fixture;
  const text = fn.toString();
  assert.doesNotMatch(text, /INCOMPLETE|switch\s*\(|Reflect\.|Function\(/);
  if (phased) assert.match(text, /try\s*\{/);
  const oracle = (left,right,third) => {
    const operand = form >= 5 ? payloadNaN : form === 2 ? left * right : form === 3 ? right : left;
    const result = operations[op](operand);
    return form === 3 ? left + result : form === 4 ? result ** 2 : result;
  };
  for (const left of values) for (const right of values) {
    const args = [left, right, 2];
    compare(invoke(fn, args), invoke(oracle, args), phased); ++scenarios;
  }
  // Each execution receives fresh coercion hooks. Preserve getter/call order,
  // conversion hint, count and the original thrown token at every boundary.
  for (const a of [2, '2', 2n, symbol]) for (const b of [3, '3', 2n, symbol])
    for (const failure of ['none', 'left.get', 'left.call', 'right.get', 'right.call', 'third.get', 'third.call']) {
      const make = log => ['left','right','third'].map((name, index) => ({
        get [Symbol.toPrimitive]() {
          log.push(`${name}.get`); if (failure === `${name}.get`) throw token;
          return function(hint) {
            assert.equal(this, argsByName.get(name));
            log.push(`${name}.call:${hint}`); if (failure === `${name}.call`) throw token;
            return [a,b,2][index];
          };
        }
      }));
      let argsByName;
      const actualLog = [], expectedLog = [];
      const actualArgs = make(actualLog); argsByName = new Map(actualArgs.map((arg,i)=>[['left','right','third'][i],arg]));
      const actual = invoke(fn, actualArgs);
      const expectedArgs = make(expectedLog); argsByName = new Map(expectedArgs.map((arg,i)=>[['left','right','third'][i],arg]));
      const expected = invoke(oracle, expectedArgs);
      compare(actual, expected, phased); assert.deepEqual(actualLog, expectedLog); ++scenarios;
    }
}
console.log(JSON.stringify({functions: fixtures.length, scenarios,
  evidence: 'decoded profiles; native unary/binary composition; results, signed zero, NaN, BigInt/Symbol, coercion order and exceptions'}));
