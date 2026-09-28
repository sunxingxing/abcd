import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--arrow-arguments'], {encoding: 'utf8', timeout: 60_000});
assert.ok(!source.includes('INCOMPLETE'));
// Test embedding of the existing v7 snapshot capability. Preserve actual count,
// current formal slots and the untouched tail; never substitute parent arguments.
const nativeArguments = Function('return arguments');
const poison = Object.getOwnPropertyDescriptor((function () { return arguments; })(), 'callee');
let snapshots = 0;
const snapshotArguments = (carrier, slots) => {
  ++snapshots;
  const values = Array.from(carrier, (value, index) => index < slots.length ? slots[index] : value);
  const result = Reflect.apply(nativeArguments, undefined, values);
  delete result.callee;
  Object.defineProperty(result, 'caller', poison);
  Object.defineProperty(result, 'callee', poison);
  return result;
};
const functions = Function('snapshotArguments', `"use strict"; ${source.replace('export const functions', 'const functions')} return functions;`)(snapshotArguments);
assert.equal(functions.length, 3);
for (const parent of functions) {
  let entered = false, get;
  parent(() => !entered && (entered = true), () => Symbol('outer-cell'), child => { get = child; });
  assert.equal(get.name, 'get');
  assert.equal(get.length, 1);
  assert.equal(Object.hasOwn(get, 'prototype'), false);
  assert.throws(() => new get(), TypeError);
  assert.equal(Object.getPrototypeOf(get), Object.getPrototypeOf(async () => {}));
  const resumes = [], inputs = [];
  const pending = Array.from({length: 3}, (_, index) => {
    const input = new Promise(resolve => resumes.push(resolve));
    const values = [input, index, Symbol('tail'), {}]; inputs.push(values);
    return Reflect.apply(get, {wrongReceiver: true}, values);
  });
  for (const resume of resumes.reverse()) resume();
  const results = await Promise.all(pending);
  results.forEach((result, index) => {
    assert.equal(Object.prototype.toString.call(result), '[object Arguments]');
    assert.deepEqual(Array.from(result), inputs[index]);
    assert.throws(() => result.callee, TypeError);
  });
  assert.equal((await get()).length, 0);
  assert.deepEqual(Array.from(await get(undefined)), [undefined]);
  for (const payload of [undefined, null, false, 0, Symbol('reject'), {}]) {
    const before = snapshots;
    const result = await get(Promise.reject(payload)).then(() => assert.fail('must reject'), error => error);
    assert.ok(Object.is(result, payload));
    assert.equal(snapshots, before);
  }
  const log = [];
  const promise = get({then(resolve) { log.push('then'); resolve(); }}).then(() => log.push('fulfilled'));
  log.push('returned'); queueMicrotask(() => log.push('microtask'));
  await promise;
  assert.deepEqual(log, ['returned', 'then', 'microtask', 'fulfilled']);
}
