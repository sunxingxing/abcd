import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {Session} from 'node:inspector';

const source = execFileSync(process.argv[2], {encoding: 'utf8'});
const {cases, debugCases, receivers} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const expected = [NaN, Infinity, undefined, null, true, false];
const profileCount = 3;
const forms = 2;
assert.equal(receivers.length, profileCount * forms * 2);
for (let i = 0; i < receivers.length; ++i) {
  const recovered = receivers[i];
  const callback = Math.floor(i / 2) % forms === 1;
  for (const receiver of [undefined, null, false, -0, NaN, 2n, Symbol(), 'text', {}, new Proxy({}, {})]) {
    let calls = 0;
    const result = Reflect.apply(recovered, receiver, [value => {
      ++calls;
      assert.ok(Object.is(value, receiver));
      return 'ignored';
    }]);
    assert.ok(Object.is(result, receiver));
    assert.equal(calls, callback ? 1 : 0);
    if (callback) {
      const marker = {};
      assert.throws(() => Reflect.apply(recovered, receiver, [() => {throw marker;}]),
        error => error === marker);
    }
  }
  let observed;
  const constructed = Reflect.construct(recovered, [value => {observed = value;}]);
  assert.equal(Object.getPrototypeOf(constructed), recovered.prototype);
  if (callback) assert.equal(constructed, observed);
}
assert.equal(cases.length, profileCount * expected.length * forms);
for (let profile = 0; profile < profileCount; ++profile) {
  for (let value = 0; value < expected.length; ++value) {
    const index = (profile * expected.length + value) * forms;
    for (let form = 0; form < forms; ++form) {
      const original = cases[index + form];
      // These names are mutable lexical bindings, not the VM constant table.
      const shadowed = Function('NaN', 'Infinity', 'undefined',
        `"use strict"; return (${original.toString()});`)(17, -3, 'shadow');
      for (const recovered of [original, shadowed]) {
        assert.equal(recovered.length, form);
        if (form === 0) {
          assert.ok(Object.is(recovered(), expected[value]));
          continue;
        }
        const events = [];
        const actual = recovered(function(argument) {
          assert.equal(this, undefined);
          events.push(argument);
          return {different: true};
        });
        assert.ok(Object.is(actual, expected[value]));
        assert.equal(events.length, 1);
        assert.ok(Object.is(events[0], expected[value]));
        const marker = {};
        assert.throws(() => recovered(argument => {
          assert.ok(Object.is(argument, expected[value]));
          throw marker;
        }), error => error === marker);
        assert.throws(() => recovered(null), TypeError);
      }
    }
  }
}

// Real debugger notifications, not a substring-only assertion. Side effects
// must stay between the two statements; abrupt completion skips the second.
assert.equal(debugCases.length, cases.length);
const session = new Session();
session.connect();
session.post('Debugger.enable');
let events = [];
session.on('Debugger.paused', () => {
  events.push('pause');
  session.post('Debugger.resume');
});
try {
  for (let i = 0; i < debugCases.length; ++i) {
    const recovered = debugCases[i];
    const value = expected[Math.floor(i / forms) % expected.length];
    const callback = i % forms === 1;
    events = [];
    const result = recovered(argument => {
      assert.ok(Object.is(argument, value));
      events.push('call');
      return {};
    });
    assert.ok(Object.is(result, value));
    assert.deepEqual(events, callback ? ['pause', 'call', 'pause'] : ['pause', 'pause']);
    if (callback) {
      events = [];
      const marker = {};
      assert.throws(() => recovered(() => {
        events.push('throw');
        throw marker;
      }), error => error === marker);
      assert.deepEqual(events, ['pause', 'throw']);
    }
  }
} finally {
  session.disconnect();
}
