import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const earlyPhi = process.argv[3] === '--early-phi';
const directRead = process.argv[3] === '--read';
const conditional = earlyPhi || process.argv[3] === '--conditional';
const computedCall = process.argv[3] === '--computed-call';
const computed = process.argv[3] === '--computed' || computedCall;
const source = execFileSync(process.argv[2], [directRead ? '--read-source' : earlyPhi ? '--early-phi-source' : computedCall ? '--computed-call-source' : computed ? '--computed-source' : conditional ? '--conditional-source' : '--staged-source'], { encoding: 'utf8' });
if (conditional || computed) assert.ok(source.includes(' ? ') && !source.includes('if ('));
if (directRead) assert.match(source, /\.__proto__ = \$arg0\.value/);
const recovered = Function(`return ${source}`)();
// Independent bytecode behavior: the scalar fixture uses false/number zero;
// computed fixtures explicitly apply ToBoolean. Buffered own-field definitions
// occur after the selected value, with the original definition count.
function reference(input) {
  try {
    const buffered = earlyPhi ? undefined : { ['__proto__']: null };
    if (directRead) Object.defineProperty(buffered, '__proto__', {
      value: input === false || input === 0 ? null : 7,
      writable: true, enumerable: true, configurable: true,
    });
    const value = directRead ? input.value : computed ? (input ? (computedCall ? input.value() : input.value) : null) : input === false || input === 0 ? null : conditional ? 7 : input.value;
    const result = earlyPhi ? {} : buffered;
    for (let i = 0; i < (directRead || computed ? 1 : 2); ++i) Object.defineProperty(result, '__proto__', {
      value, writable: true, enumerable: true, configurable: true,
    });
    return result;
  } catch (error) { return error; }
}
let cases = 0;
const values = [undefined, null, false, true, 0, -0, 1, NaN, Infinity,
  '', 'text', 1n, Symbol('field'), {}, [], () => {}];
for (const payload of values) for (const fail of (computedCall ? [0, 1, 2] : [0, 1])) {
  const throws = fail !== 0;
  const run = fn => {
    const trace = [];
    const input = new Proxy({}, {
      get(target, key, receiver) {
        trace.push([key, receiver === input]);
        if (fail === 1) throw payload;
        if (!computedCall) return payload;
        const method = function() {
          trace.push(['call', this === input]);
          if (fail === 2) throw payload;
          return payload;
        };
        Object.defineProperty(method, 'call', {get() { assert.fail('mutable call helper'); }});
        return method;
      },
    });
    const result = fn(input);
    assert.deepEqual(trace, conditional ? [] : computedCall && fail !== 1 ? [['value', true], ['call', true]] : [['value', true]]);
    if (throws && !conditional) assert.ok(Object.is(result, payload));
    else {
      assert.equal(Object.getPrototypeOf(result), Object.prototype);
      assert.deepEqual(Reflect.ownKeys(result), ['__proto__']);
      const property = Object.getOwnPropertyDescriptor(result, '__proto__');
      assert.ok(Object.is(property.value, conditional ? 7 : payload));
      assert.deepEqual([property.writable, property.enumerable, property.configurable], [true, true, true]);
    }
    return result;
  };
  run(reference); run(recovered); ++cases;
}
for (const input of [false, 0, -0, true, 1, '', 'text', 1n, Symbol('input')]) {
  const actual = recovered(input), expected = reference(input);
  if (computedCall && expected instanceof TypeError) assert.ok(actual instanceof TypeError);
  else assert.deepEqual(actual, expected);
  ++cases;
}
for (const input of [undefined, null]) {
  if (conditional || computed) assert.deepEqual(recovered(input), reference(input));
  else {
    assert.ok(recovered(input) instanceof TypeError);
    assert.ok(reference(input) instanceof TypeError);
  }
  ++cases;
}
assert.notEqual(recovered(false), recovered(false));
console.log(`${cases} phased staged literal cases passed`);
