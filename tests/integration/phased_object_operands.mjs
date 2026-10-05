import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--object-pair-call-source'], {encoding: 'utf8'});
assert.equal((source.match(/\["__proto__"\]:/g) ?? []).length, 2);
assert.doesNotMatch(source, /\.__proto__ = /);
assert.doesNotMatch(source, /\.call\(/);
const recovered = Function(`"use strict"; return ${source}`)();

// Independent instruction semantics: two separate selected member invocations,
// each followed by an own definition. The second invocation obtains its callee
// again; failures retain the original thrown token and completed prior effects.
function reference(input) {
  try {
    const object = {['__proto__']: null};
    for (let field = 0; field < 2; ++field) {
      const value = input ? input.value() : null;
      Object.defineProperty(object, '__proto__', {
        value, writable: true, enumerable: true, configurable: true,
      });
    }
    return object;
  } catch (error) { return error; }
}

const tokens = [undefined, null, false, 0, -0, NaN, Infinity, -Infinity,
  '', 'token', 1n, Symbol('token'), {}, [], function original() {}];
let cases = 0;
for (const token of tokens) for (let failure = 0; failure <= 4; ++failure) {
  function run(fn) {
    const trace = [];
    let ordinal = 0;
    const input = {
      [Symbol.toPrimitive]() { assert.fail('ToBoolean must not invoke conversion'); },
      get value() {
        const field = ++ordinal;
        trace.push(['get', field]);
        if (failure === field * 2 - 1) throw token;
        const callable = function() {
          trace.push(['call', field, this === input]);
          if (failure === field * 2) throw token;
          return field === 1 ? input : token;
        };
        Object.defineProperty(callable, 'call', {get() { assert.fail('mutable invocation helper'); }});
        return callable;
      },
    };
    const result = fn(input);
    if (failure) assert.ok(Object.is(result, token));
    else {
      assert.equal(Object.getPrototypeOf(result), Object.prototype);
      assert.deepEqual(Reflect.ownKeys(result), ['__proto__']);
      const property = Object.getOwnPropertyDescriptor(result, '__proto__');
      assert.ok(Object.is(property.value, token));
      assert.deepEqual([property.writable, property.enumerable, property.configurable], [true, true, true]);
    }
    return trace;
  }
  assert.deepEqual(run(recovered), run(reference));
  ++cases;
}
for (const input of [undefined, null, false, 0, -0, NaN, '', 0n]) {
  const actual = recovered(input), expected = reference(input);
  assert.deepEqual(Object.getOwnPropertyDescriptors(actual), Object.getOwnPropertyDescriptors(expected));
  assert.notEqual(actual, recovered(input));
  ++cases;
}
console.log(`${cases} phased object operand cases preserve repeated callee reads, receiver, effects and thrown identity`);
