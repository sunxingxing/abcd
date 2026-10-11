import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Both complete factories include repeated descriptor replacement. No reference
// body, expected name table or sidecar is ever passed to the ABC-only engine.
const fixtures = execFileSync(process.argv[2], ['--async-json'], {encoding: 'utf8'})
  .trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length, 6);
let scenarios = 0;
function oracle(isStatic) {
  return isStatic ? `(() => class Accessors {
    static get value() { return this._value; }
    static async value(input) { await input; return this; }
    static get value() { return this._value; }
    static async value(input) { await input; return this; }
  })` : `(() => class Accessors {
    get value() { return this._value; }
    async value(input) { await input; return this; }
    get value() { return this._value; }
    async value(input) { await input; return this; }
  })`;
}
async function exercise(source, isStatic, receiver, mode) {
  const context = vm.createContext({});
  const factory = vm.runInContext(`'use strict'; ${source}`, context);
  const Type = factory(), Other = factory();
  assert.notEqual(Type, Other); assert.notEqual(Type.prototype, Other.prototype);
  assert.equal(Type.name, 'Accessors'); assert.equal(Type.length, 0);
  const home = isStatic ? Type : Type.prototype;
  const peer = isStatic ? Other : Other.prototype;
  const descriptor = Object.getOwnPropertyDescriptor(home, 'value');
  assert.equal(descriptor.enumerable, false); assert.equal(descriptor.configurable, true);
  assert.equal(descriptor.writable, true); assert.ok(!('get' in descriptor) && !('set' in descriptor));
  const fn = descriptor.value;
  assert.notEqual(fn, Object.getOwnPropertyDescriptor(peer, 'value').value);
  assert.equal(fn.name, 'value'); assert.equal(fn.length, 1); assert.equal(Object.hasOwn(fn, 'prototype'), false);
  assert.equal(Object.getPrototypeOf(fn), vm.runInContext('Object.getPrototypeOf(async function(){})', context));
  assert.throws(() => Reflect.construct(fn, []), error => error.name === 'TypeError');
  const trace = [], reason = {}, input = {};
  let settle;
  if (mode !== 'primitive') Object.defineProperty(input, 'then', {get() {
    trace.push('get-then');
    if (mode === 'getter-throw') throw reason;
    if (mode === 'noncallable') return 42;
    return function (resolve, reject) {
      assert.equal(this, input); trace.push('call-then');
      if (mode === 'deferred') settle = () => resolve();
      else if (mode === 'reject') reject(reason);
      else if (mode === 'throw') throw reason;
      else { resolve(); if (mode === 'resolve-then-throw') throw reason; }
    };
  }});
  const pending = Reflect.apply(fn, receiver, [mode === 'primitive' ? undefined : input]);
  assert.equal(Object.getPrototypeOf(pending), vm.runInContext('Promise.prototype', context));
  trace.push('returned');
  let result, rejected = false, completed = false;
  const observed = pending.then(value => {
    completed = true; result = value; trace.push('fulfilled');
  }, error => {
    completed = true; result = error; rejected = true; trace.push('rejected');
  });
  queueMicrotask(() => trace.push('external-microtask'));
  if (mode === 'deferred') {
    await Promise.resolve(); await Promise.resolve();
    assert.equal(completed, false); assert.equal(typeof settle, 'function');
    trace.push('resume'); settle();
  }
  await observed;
  assert.equal(rejected, ['reject', 'getter-throw', 'throw'].includes(mode));
  assert.ok(Object.is(result, rejected ? reason : receiver));
  // Poison ambient invocation helpers after retrieving the actual native
  // method; the second invocation still uses the same recovered body.
  vm.runInContext('Function.prototype.call = Function.prototype.apply = Function.prototype.bind = () => { throw 17; };', context);
  assert.equal(await Reflect.apply(fn, receiver, [undefined]), receiver);
  return {trace, keys: Reflect.ownKeys(home)};
}
for (const {source, static: isStatic} of fixtures) {
  assert.ok(!source.includes('INCOMPLETE'));
  for (const receiver of [undefined, null, false, -0, NaN, 'text', Symbol(), {}])
    for (const mode of ['primitive', 'resolve', 'reject', 'getter-throw', 'noncallable', 'deferred', 'throw', 'resolve-then-throw']) {
      assert.deepEqual(await exercise(source, isStatic, receiver, mode),
        await exercise(oracle(isStatic), isStatic, receiver, mode)); ++scenarios;
    }
  // Concurrent invocations retain their own receiver across out-of-order resume.
  const create = vm.runInNewContext(`'use strict'; ${source}`), Type = create();
  const fn = (isStatic ? Type : Type.prototype).value;
  const first = {}, second = {};
  let resumeFirst, resumeSecond, firstDone = false;
  const a = Reflect.apply(fn, first, [new Promise(resolve => { resumeFirst = resolve; })]);
  const b = Reflect.apply(fn, second, [new Promise(resolve => { resumeSecond = resolve; })]);
  a.then(() => { firstDone = true; }); resumeSecond();
  assert.equal(await b, second); assert.equal(firstDone, false);
  resumeFirst(); assert.equal(await a, first); ++scenarios;
  const missingAwait = source.replace(/\bawait /g, '');
  assert.notEqual(missingAwait, source);
  assert.doesNotThrow(() => new vm.Script(`'use strict'; ${missingAwait}`));
  await assert.rejects(async () => assert.deepEqual(
    await exercise(missingAwait, isStatic, null, 'resolve'),
    await exercise(oracle(isStatic), isStatic, null, 'resolve')));
  // This fixture has no textual `this` literal. Mutate the receiver itself,
  // whether recovery retains a snapshot assignment or returns it directly.
  const wrongReceiver = source.replace(/\bthis\b/g, 'null');
  assert.notEqual(wrongReceiver, source);
  assert.doesNotThrow(() => new vm.Script(`'use strict'; ${wrongReceiver}`));
  await assert.rejects(() => exercise(wrongReceiver, isStatic, {}, 'primitive'));
}
console.log(`Defined async methods: ${scenarios} complete class/descriptor/await/receiver/throw/concurrent scenarios passed.`);
