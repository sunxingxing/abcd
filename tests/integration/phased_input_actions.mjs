import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const cases = execFileSync(process.argv[2], ['source'], {encoding: 'utf8'})
  .trim().split('\n').map(JSON.parse);
assert.equal(cases.length, 36);
let comparisons = 0;
for (const item of cases) {
  const trace = [];
  const context = vm.createContext({trace});
  // Test embedding implements the same explicit v7 capability as
  // argument_snapshot.mjs, in the function's realm, using retained intrinsics.
  const {fn, realm, symbol, poison, iterator, iteratorKey, prototype} = vm.runInContext(`
    "use strict";
    const hostRealm = globalThis, hostSymbol = Symbol;
    const apply = Reflect.apply, define = Object.defineProperty;
    const descriptor = Object.getOwnPropertyDescriptor;
    const strictArguments = function () { 'use strict'; return arguments; };
    const poison = descriptor(strictArguments(), 'callee');
    const makeArguments = Function('return arguments');
    const iterator = Array.prototype.values, iteratorKey = Symbol.iterator;
    const snapshotArguments = (carrier, slots) => {
      trace.push('snapshot');
      const values = [];
      for (let i = 0; i < carrier.length; ++i)
        define(values, i, {value: i < slots.length ? slots[i] : carrier[i],
          enumerable: true, writable: true, configurable: true});
      const result = apply(makeArguments, undefined, values);
      delete result.callee;
      // V8 cross-context invocation may select its caller realm iterator; the
      // explicit embedding capability promises this function's pinned realm.
      define(result, iteratorKey, {value: iterator, writable: true, enumerable: false, configurable: true});
      define(result, 'caller', poison); define(result, 'callee', poison);
      return result;
    };
    ({fn: (${item.source}), realm: hostRealm, symbol: hostSymbol, poison,
      iterator: Array.prototype.values, iteratorKey: Symbol.iterator,
      prototype: Object.prototype});
  `, context);
  context.Symbol = function wrongPublicSymbol() { throw new Error('public Symbol read'); };
  let current = {initial: true}, getterFails = false;
  const failure = {failure: true};
  Object.defineProperty(context, 'probe', {configurable: true, get() {
    trace.push('global'); if (getterFails) throw failure; return current;
  }});
  const prelude = item.kind === 4 ? ['snapshot'] : item.kind === 5 ? ['global'] : [];
  const snapshot = (actual, args) => {
    assert.equal(Object.getPrototypeOf(actual), prototype);
    assert.equal(Array.isArray(actual), false);
    assert.equal(Object.prototype.toString.call(actual), '[object Arguments]');
    assert.deepEqual(Object.getOwnPropertyDescriptor(actual, 'length'), {
      value: args.length, writable: true, enumerable: false, configurable: true,
    });
    assert.deepEqual(Object.getOwnPropertyDescriptor(actual, iteratorKey), {
      value: iterator, writable: true, enumerable: false, configurable: true,
    });
    for (const name of ['caller', 'callee'])
      assert.deepEqual(Object.getOwnPropertyDescriptor(actual, name), {...poison});
    for (let i = 0; i < args.length; ++i)
      assert.deepEqual(Object.getOwnPropertyDescriptor(actual, i), {
        value: args[i], writable: true, enumerable: true, configurable: true,
      });
    assert.deepEqual(Reflect.ownKeys(actual), [
      ...args.map((_, i) => String(i)), 'length', 'caller', 'callee', iteratorKey,
    ]);
  };
  const checkValue = (actual, receiver, args, savedGlobal) => {
    switch (item.kind) {
      case 0: assert.equal(actual, receiver); break;
      case 1: assert.equal(actual, fn); break;
      case 2: assert.equal(actual, realm); break;
      case 3: assert.equal(actual, symbol); break;
      case 4: snapshot(actual, args); break;
      case 5: assert.equal(actual, savedGlobal); break;
      default: assert.fail('unknown input');
    }
  };
  for (const receiver of [undefined, null, 17, {receiver: true}]) {
    for (const extra of [[], [{extra: true}, -0]]) {
      const savedGlobal = current;
      const callback = () => { trace.push('callback'); current = {changed: true}; };
      const args = [callback, ...extra]; trace.length = 0;
      const actual = Reflect.apply(fn, receiver, args);
      checkValue(actual, receiver, args, savedGlobal);
      assert.deepEqual(trace, [...prelude, 'callback']); ++comparisons;
      trace.length = 0;
      const again = Reflect.apply(fn, receiver, args);
      if (item.kind === 4) assert.notEqual(actual, again);
      assert.deepEqual(trace, [...prelude, 'callback']); ++comparisons;
    }
    trace.length = 0;
    assert.equal(Reflect.apply(fn, receiver, [() => {trace.push('throw'); throw failure;}]), undefined);
    assert.deepEqual(trace, [...prelude, 'throw']); ++comparisons;
  }
  trace.length = 0;
  assert.equal(fn(), undefined); // Input is still evaluated before a missing callback throws.
  assert.deepEqual(trace, prelude); ++comparisons;
  if (item.kind === 5) {
    getterFails = true; trace.length = 0;
    assert.equal(fn(() => assert.fail('callback after failed GetValue')), undefined);
    assert.deepEqual(trace, ['global']); getterFails = false; ++comparisons;
  }
  trace.length = 0;
  const receiver = {outer: true}, nestedReceiver = {inner: true}, saved = current;
  const nestedArgs = [() => trace.push('nested')];
  let nested;
  const args = [() => {trace.push('outer'); nested = Reflect.apply(fn, nestedReceiver, nestedArgs);}];
  const result = Reflect.apply(fn, receiver, args);
  checkValue(result, receiver, args, saved); checkValue(nested, nestedReceiver, nestedArgs, saved);
  if (item.kind === 4) assert.notEqual(result, nested);
  assert.deepEqual(trace, [...prelude, 'outer', ...prelude, 'nested']); ++comparisons;
}
console.log(JSON.stringify({sources: cases.length, comparisons}));
