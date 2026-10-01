import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8'});
const {cases, syntax} = await import(`data:text/javascript,${encodeURIComponent(source)}`);
assert.equal(cases.length, 72);
assert.equal(syntax.length, 4);
let scenarios = 0;
function verifyDecoded(item, fn = item.fn) {
  let expected = item.seed;
  for (let i = 0; i < item.count; ++i) item.decrement ? --expected : ++expected;
  for (const thrown of [null, false, -0, 19n, Symbol('error'), {error: true}]) {
    for (const fail of [false, true]) {
      const trace = [];
      let value, error, failed = false;
      try {
        value = fn(function (old, updated) {
          'use strict';
          assert.equal(this, undefined);
          assert.equal(arguments.length, 2);
          trace.push([old, updated]);
          if (fail) throw thrown;
          return 'ignored callback result';
        });
      } catch (e) { failed = true; error = e; }
      assert.equal(trace.length, 1);
      assert.ok(Object.is(trace[0][0], item.seed));
      assert.ok(Object.is(trace[0][1], expected));
      if (fail && !item.caught) {
        assert.equal(failed, true); assert.ok(Object.is(error, thrown));
      } else {
        assert.equal(failed, false);
        if (fail) assert.ok(Object.is(value, item.seed));
        else {
          assert.equal(Object.getPrototypeOf(value), Object.prototype);
          assert.deepEqual(Reflect.ownKeys(value), ['old', 'updated']);
          for (const [key, want] of [['old', item.seed], ['updated', expected]]) {
            const desc = Object.getOwnPropertyDescriptor(value, key);
            assert.ok(Object.is(desc.value, want));
            assert.deepEqual({...desc, value: undefined}, {
              value: undefined, writable: true, enumerable: true, configurable: true,
            });
          }
        }
      }
      ++scenarios;
    }
  }
}
for (const item of cases) verifyDecoded(item);

// These four hand-built ASTs test native syntax, not arbitrary ABC-domain
// acceptance. The independent oracle performs one number-hint conversion.
for (const item of syntax) {
  function reference(input) {
    let local = input;
    return item.postfix ? (item.decrement ? local-- : local++)
        : (item.decrement ? --local : ++local);
  }
  for (const input of [undefined, null, false, true, -0, NaN, Infinity, -Infinity,
    2147483647, -2147483648, 1e20, 1n, 0n, -9n, '12', 'bad', '', Symbol('input')]) {
    let want, actual, expectedError, actualError;
    try { want = reference(input); } catch (e) { expectedError = e.constructor; }
    try { actual = item.fn(input); } catch (e) { actualError = e.constructor; }
    assert.equal(actualError, expectedError);
    if (!actualError) assert.ok(Object.is(actual, want));
    ++scenarios;
  }
  for (const converted of [0, -0, NaN, Infinity, 17n, -19n, Symbol('converted')]) {
    for (const failure of [false, true]) {
      const token = {coercion: true};
      const run = fn => {
        const trace = [];
        const input = {[Symbol.toPrimitive](hint) {
          trace.push(hint); if (failure) throw token; return converted;
        }};
        let value, error, failed = false;
        try { value = fn(input); } catch (e) { error = e; failed = true; }
        return {value, error, failed, trace, input};
      };
      const want = run(reference), actual = run(item.fn);
      assert.deepEqual(actual.trace, ['number']);
      assert.deepEqual(actual.trace, want.trace);
      assert.equal(actual.failed, want.failed);
      if (!actual.failed) assert.ok(Object.is(actual.value, want.value));
      else if (failure) assert.equal(actual.error, token);
      else assert.equal(actual.error.constructor, want.error.constructor);
      assert.equal(Reflect.ownKeys(actual.input).length, 1);
      ++scenarios;
    }
  }
}

// Valid syntax mutations must be caught by behavior, including a wrong
// update result and an update corrupting the still-observable old snapshot.
const item = cases.find(c => !c.caught && !c.decrement && c.count === 1 && c.seed === 7);
const text = item.fn.toString();
const update = text.match(/\(\+\+(\$update\d+)\)/);
assert.ok(update);
const target = update[1];
const old = text.match(/\(\$update\d+ = (\$value\d+)\)/);
assert.ok(old);
const mutations = [
  text.replace(`(++${target})`, `(${target}++)`),
  text.replace(`(++${target})`, `(--${target})`),
  text.replace(`(++${target})`, `(++${old[1]})`),
];
for (const changed of mutations) {
  const fn = Function(`return (${changed})`)();
  assert.throws(() => verifyDecoded(item, fn), assert.AssertionError);
}
console.log(`numeric updates: ${scenarios} scenarios, ${mutations.length} valid-syntax mutations`);
