import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {Session} from 'node:inspector';

const source = execFileSync(process.argv[2], {encoding: 'utf8'});
const {cases, debugCases, receivers, functions, namedFunctions, createdFunctions, globalFunctions, realms, hostRealms, symbols, thrownValues, thrownReceivers} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const expected = [NaN, Infinity, undefined, null, true, false];
const profileCount = 3;
const forms = 2;
const observeThrow = (fn, receiver, args) => {
  let threw = false, result;
  try { Reflect.apply(fn, receiver, args); }
  catch (value) { threw = true; result = value; }
  assert.equal(threw, true);
  return result;
};
assert.equal(thrownValues.length, profileCount * expected.length * forms);
for (let i = 0; i < thrownValues.length; ++i) {
  const value = expected[Math.floor(i / forms) % expected.length];
  const fn = thrownValues[i];
  let calls = 0;
  assert.ok(Object.is(observeThrow(fn, null, [input => {
    ++calls; assert.ok(Object.is(input, value)); return {ignored: true};
  }]), value));
  assert.equal(calls, i % forms);
  if (i % forms) {
    const earlier = {};
    assert.equal(observeThrow(fn, null, [() => { throw earlier; }]), earlier);
  }
}
assert.equal(thrownReceivers.length, profileCount);
let coerced = 0;
const opaque = new Proxy({}, {get() { ++coerced; throw new Error('unexpected coercion'); }});
for (const fn of thrownReceivers)
  for (const receiver of [undefined, null, -0, NaN, 7n, 'text', Symbol('payload'), opaque])
    assert.ok(Object.is(observeThrow(fn, receiver, []), receiver));
assert.equal(coerced, 0);
assert.equal(symbols.length, profileCount * 2 * forms);
const intrinsicSymbol = Symbol;
const symbolProperty = Object.getOwnPropertyDescriptor(globalThis, 'Symbol');
let symbolReads = 0;
try {
  for (const accessor of [false, true]) {
    Object.defineProperty(globalThis, 'Symbol', accessor ? {
      configurable: true, get() { ++symbolReads; throw new Error('public Symbol lookup'); },
    } : {configurable: true, writable: true, value: {notIntrinsic: true}});
    for (let i = 0; i < symbols.length; ++i) {
      const fn = symbols[i];
      let calls = 0;
      const value = Reflect.apply(fn, {notIntrinsic: true}, [input => {
        ++calls; assert.equal(input, intrinsicSymbol); return {ignored: true};
      }]);
      assert.equal(value, intrinsicSymbol);
      assert.equal(typeof value('description'), 'symbol');
      assert.throws(() => Reflect.construct(value, []), TypeError);
      assert.equal(calls, i % forms);
      if (i % forms) {
        const thrown = {};
        assert.throws(() => fn(() => {throw thrown;}), error => error === thrown);
        assert.equal(fn(() => assert.equal(fn(() => 0), intrinsicSymbol)), intrinsicSymbol);
      }
    }
  }
  assert.equal(symbolReads, 0);
} finally { Object.defineProperty(globalThis, 'Symbol', symbolProperty); }
for (const functions of [realms, hostRealms]) {
  assert.equal(functions.length, profileCount * 2 * forms);
  for (let i = 0; i < functions.length; ++i) {
    const fn = functions[i];
    let calls = 0;
    assert.equal(Reflect.apply(fn, {notRealm: true}, [value => {
      ++calls; assert.equal(value, globalThis); return {ignored: true};
    }]), globalThis);
    assert.equal(calls, i % forms);
    if (i % forms) {
      const thrown = {};
      assert.throws(() => fn(() => {throw thrown;}), error => error === thrown);
      assert.equal(fn(() => assert.equal(fn(() => 0), globalThis)), globalThis);
    }
  }
}
assert.equal(globalFunctions.length, profileCount * 2 * forms);
const previousConstant = Object.getOwnPropertyDescriptor(globalThis, 'constant');
try {
  for (const fn of globalFunctions) {
    let reads = 0;
    Object.defineProperty(globalThis, 'constant', {configurable: true, get() {
      ++reads;
      return {notSelf: true};
    }});
    assert.equal(fn(value => assert.equal(value, fn)), fn);
    assert.equal(reads, 1);
    const marker = {};
    Object.defineProperty(globalThis, 'constant', {configurable: true, get() {throw marker;}});
    assert.throws(() => fn(() => assert.fail('callback after failed global read')), error => error === marker);
  }
} finally {
  if (previousConstant) Object.defineProperty(globalThis, 'constant', previousConstant);
  else delete globalThis.constant;
}
const creationNames = ['', 'eval', 'with space', '$self0', '__proto__'];
assert.equal(createdFunctions.length, profileCount * creationNames.length * 2 * forms);
for (let i = 0; i < createdFunctions.length; ++i) {
  const fn = createdFunctions[i];
  const callback = i % forms === 1;
  const arrow = Math.floor(i / forms) % 2 === 1;
  assert.equal(fn.name, creationNames[Math.floor(i / (forms * 2)) % creationNames.length]);
  assert.equal(fn.length, callback ? 1 : 0);
  const descriptor = Object.getOwnPropertyDescriptor(fn, 'name');
  assert.equal(descriptor.writable, false);
  assert.equal(descriptor.enumerable, false);
  assert.equal(descriptor.configurable, true);
  let calls = 0;
  const observe = value => { ++calls; assert.equal(value, fn); return 'ignored'; };
  assert.equal(Reflect.apply(fn, {wrong: true}, [observe]), fn);
  assert.equal(calls, callback ? 1 : 0);
  assert.equal(Object.hasOwn(fn, 'prototype'), !arrow);
  if (arrow) assert.throws(() => Reflect.construct(fn, [observe]), TypeError);
  else assert.equal(Reflect.construct(fn, [observe]), fn);
  Object.defineProperty(fn, 'name', {value: 'changed'});
  assert.equal(fn(observe), fn);
  if (callback) {
    const marker = {};
    assert.throws(() => fn(() => {throw marker;}), error => error === marker);
    assert.equal(fn(value => assert.equal(value(observe), fn)), fn);
  }
}
assert.equal(functions.length, profileCount * forms * 2);
assert.deepEqual(namedFunctions.map(fn => fn.name), ['$arg0', '__proto__', 'MethodSelf1', 'ClassValue']);
for (const fn of namedFunctions) assert.equal(fn(value => assert.equal(value, fn)), fn);
for (let i = 0; i < functions.length; ++i) {
  const recovered = functions[i];
  const callback = Math.floor(i / 2) % forms === 1;
  assert.equal(recovered.name, 'constant');
  assert.equal(recovered.length, callback ? 1 : 0);
  let calls = 0;
  const observe = value => {++calls; assert.equal(value, recovered);};
  assert.equal(Reflect.apply(recovered, {unrelated: true}, [observe]), recovered);
  assert.equal(calls, callback ? 1 : 0);
  assert.equal(Reflect.construct(recovered, [observe]), recovered);
  let constant = recovered;
  const alias = constant;
  constant = () => 'replacement';
  Object.defineProperty(recovered, 'name', {value: 'renamed'});
  assert.equal(alias(observe), recovered);
  assert.notEqual(alias(observe), constant);
  if (callback) {
    const marker = {};
    assert.throws(() => recovered(() => {throw marker;}), error => error === marker);
    assert.equal(recovered(value => {
      assert.equal(value, recovered);
      assert.equal(value(observe), recovered);
    }), recovered);
  }
}
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
