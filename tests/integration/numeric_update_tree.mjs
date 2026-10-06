import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';

const source = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8'});
const {cases, syntax, arbitrary} = await import(`data:text/javascript,${encodeURIComponent(source)}`);
assert.equal(cases.length, 108);
assert.equal(syntax.length, 4);
assert.equal(arbitrary.length, 24);
function nodes(text) {
  const result = [], pending = [parseJavaScript(`(${text})`)];
  while (pending.length) {
    const node = pending.pop(); result.push(node);
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) {
        for (const child of value) if (child?.type) pending.push(child);
      } else if (value?.type) pending.push(value);
    }
  }
  return result;
}
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
for (const item of cases.filter(c => c.count === 1)) {
  const ast = nodes(item.fn.toString());
  const update = ast.find(n => n.type === 'UpdateExpression');
  const result = ast.find(n => n.type === 'Property' && n.key.name === 'updated').value;
  assert.equal(update.argument.name, result.name,
    'materialized update must use its own result storage');
}

// Actual decoded language-mode ToNumeric and INC/DEC retain arbitrary inputs.
// Conversion happens outside the protected update/callback; its exact number
// hint, throwing identity, old value and callback receiver are observable.
for (const item of arbitrary) {
  const reference = (callback, input) => {
    let converted = input;
    const old = item.convert ? converted++ : input;
    try {
      let updated = old; item.decrement ? --updated : ++updated;
      (0, callback)(old, updated);
      return {old, updated};
    } catch (error) { if (item.caught) return old; throw error; }
  };
  const values = [undefined, null, false, true, -0, NaN, Infinity, -Infinity,
    2147483647, -2147483648, 1e20, 1n, 0n, -9n, '12', 'bad', '', Symbol('input')];
  for (const converted of values) for (const custom of [false, true])
    for (const failure of ['none', 'conversion', 'callback']) {
      if (!custom && failure === 'conversion') continue;
      const token = {failure}, originalInput = {original: true};
      const run = fn => {
        const trace = [];
        const input = custom ? {[Symbol.toPrimitive](hint) {
          trace.push(['convert', hint]);
          if (failure === 'conversion') throw token;
          return converted;
        }} : converted;
        let value, error, failed = false;
        try {
          value = fn(function (old, updated) {
            'use strict'; assert.equal(this, undefined);
            assert.equal(arguments.length, 2);
            trace.push(['call', custom && old === input ? originalInput : old, updated]);
            if (failure === 'callback') throw token;
          }, input);
        } catch (e) { failed = true; error = e; }
        if (custom) {
          assert.deepEqual(Reflect.ownKeys(input), [Symbol.toPrimitive]);
          if (value === input) value = originalInput;
          else if (value && typeof value === 'object' && value.old === input)
            value = {...value, old: originalInput};
        }
        return {value, error, failed, trace};
      };
      const want = run(reference), actual = run(item.fn);
      assert.equal(actual.failed, want.failed);
      assert.deepEqual(actual.trace, want.trace);
      if (actual.failed) {
        if (want.error === token) assert.equal(actual.error, token);
        else assert.equal(actual.error.constructor, want.error.constructor);
      } else if (want.value !== null && typeof want.value === 'object') {
        assert.deepEqual(actual.value, want.value);
        assert.equal(Object.getPrototypeOf(actual.value), Object.prototype);
      } else assert.ok(Object.is(actual.value, want.value));
      ++scenarios;
    }
}

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
const ast = nodes(text);
const update = ast.find(n => n.type === 'UpdateExpression');
const target = update.argument.name;
const old = ast.find(n => n.type === 'Property' && n.key.name === 'old').value.name;
const replace = value => text.slice(0, update.start - 1) + value + text.slice(update.end - 1);
const mutations = [
  replace(`${target}++`), replace(`--${target}`), replace(`++${old}`),
];
for (const changed of mutations) {
  const fn = Function(`return (${changed})`)();
  assert.throws(() => verifyDecoded(item, fn), assert.AssertionError);
}
console.log(`numeric updates: ${scenarios} scenarios, ${mutations.length} valid-syntax mutations`);
