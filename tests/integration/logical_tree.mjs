import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['--sources'], {encoding: 'utf8'});
// References are independent hand-written JS. The C++ fixture constructs AST,
// not recovered ABC: this verifies syntax/composition, not branch recognition.
const references = [
  e => e.target.method(e.subject.value ?? e.fallback()),
  e => e.target.method((e.subject.value ?? e.fallback()) || e.extra()),
  e => e.target.method((e.subject.value ?? e.fallback()) && e.extra()),
  e => e.target.method(e.subject.value ?? (e.fallback() || e.extra())),
  e => e.target.method(e.subject.value ?? (e.fallback() && e.extra())),
  e => e.target.method(e.subject.value ? e.fallback() : e.extra()),
  e => e.target.method((e.subject.value ? e.fallback() : e.extra()) ?? e.tail()),
  e => e.target.method((e.subject.value ?? e.fallback()) ? e.extra() : e.tail()),
];
const identity = Object.freeze({marker: 'value', [Symbol.toPrimitive]() { throw new Error('unexpected coercion'); }});
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', 0n, 1n, Symbol('value'), identity];
let scenarios = 0;
function run(invoke, value, alternate, fail, mutate) {
  const events = [], failure = Object.freeze({stage: fail}), extraValue = {};
  let current = value;
  const event = name => { events.push(name); if (name === fail) throw failure; };
  const e = {
    subject: {get value() { event('value'); return current; }},
    fallback() { event('fallback'); return alternate; },
    extra() { event('extra'); return extraValue; },
    tail() { event('tail'); return alternate; },
    target: {get method() {
      event('method');
      // A callee getter can change the argument source. Moving the getter
      // across ?? must be observable, even if the final method is unchanged.
      if (mutate) current = alternate;
      return function(argument) {
        assert.equal(this, e.target);
        event('call'); return argument;
      };
    }},
  };
  try {
    const result = invoke(e);
    events.push('return');
    // Identity, -0, NaN, symbols and falsy primitives are all significant.
    if (result === extraValue) return {events, extra: true};
    return {events, result};
  } catch (error) {
    assert.equal(error, failure, 'unexpected exception or coercion');
    events.push('throw'); return {events};
  }
}
const sandbox = {};
const generated = vm.runInNewContext(source, sandbox);
assert.equal(generated.length, references.length);
for (let shape = 0; shape < references.length; ++shape) {
  for (const value of values) for (const alternate of values) {
    for (const fail of [undefined, 'method', 'value', 'fallback', 'extra', 'tail', 'call']) {
      for (const mutate of [false, true]) {
        const actual = run(e => { Object.assign(sandbox, e); return generated[shape](); }, value, alternate, fail, mutate);
        const expected = run(references[shape], value, alternate, fail, mutate);
        assert.deepEqual(actual, expected);
        ++scenarios;
      }
    }
  }
}
console.log(`Logical and conditional member arguments: ${scenarios} behavior scenarios passed`);
