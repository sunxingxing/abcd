import assert from 'node:assert/strict';
import vm from 'node:vm';
import {parseJavaScript} from './class_reference.mjs';
import {verifyCallableSignatures} from './callable_signatures.mjs';

const reference = `function factory(first, second) {
  let saved = first;
  return (value = saved, ...tail) => [value, tail.length];
}`;
const renamed = `function factory(left, right) {
  const irrelevant = right;
  let copy = left; let retained = copy;
  return (argument = retained, ...rest) => [argument, rest.length];
}`;
verifyCallableSignatures(renamed, reference);
verifyCallableSignatures('class A { field = (x = 1) => x; run() { return (x = 2) => x; } }',
  'class A { constructor() { this.field = (x = 1) => x; } run() { return (x = 2) => x; } }');
verifyCallableSignatures('class A { field = (x = 1) => x; }',
  'class A { constructor() { const saved = this; saved.field = (y = 1) => y; } }');
verifyCallableSignatures('function f(x) { if (true) { var held = x; } return (v = held) => v; }',
  'function f(y) { var copy = y; if (true) {} return (v = copy) => v; }');
verifyCallableSignatures('function f(undefined) { return (x = undefined) => x; }',
  'function f(value) { return (x = value) => x; }');
verifyCallableSignatures('function f() { let undefined = 7; return (x = undefined) => x; }',
  'function f() { let other = 7; return (x = other) => x; }');
const changes = [
  reference.replace('value = saved', 'value = second'),
  reference.replace('let saved = first', 'let saved = second'),
  reference.replace('value = saved', 'value = 0'),
  reference.replace('...tail', 'tail'),
  reference.replace('(value = saved, ...tail) =>', 'async (value = saved, ...tail) =>'),
];
const observe = async text => {
  const factory = vm.runInNewContext(text + '; factory'), callback = factory(7, 9);
  const result = callback(undefined, 'abc', 'c');
  return JSON.stringify({value: await result, async: typeof result?.then === 'function'});
};
assert.equal(await observe(renamed), await observe(reference));
for (const wrong of changes) {
  parseJavaScript(wrong);
  assert.throws(() => verifyCallableSignatures(wrong, reference), {name: 'AssertionError'});
  assert.notEqual(await observe(wrong), await observe(reference));
}
const comparisons = [
  ['function f(x) { return (y = x) => y; }', 'function f(x) { return (y = free) => y; }'],
  ['function f(x) { return (y = x) => y; }', 'function f(x) { { let x = 2; return (y = x) => y; } }'],
  ['function f(x = free) { let free = 1; return x; }', 'function f(x = 1) { let free = 1; return x; }'],
  ['function f(undefined) { return (x = undefined) => x; }', 'function f(undefined) { return (x = void 0) => x; }'],
  ['function f() { return () => this; }', 'function f() { return function() { return this; }; }'],
  ['function f() { return function*() { yield 1; }; }', 'function f() { return function() { return 1; }; }'],
  ['class A { first() { return (v = 1) => v; } second() { return (v = 2) => v; } }',
    'class A { first() { return (v = 2) => v; } second() { return (v = 1) => v; } }'],
  ['class A { first = (v = 1) => v; second = (v = 2) => v; }',
    'class A { first = (v = 2) => v; second = (v = 1) => v; }'],
  ['function f(host) { host.get((v = 1) => v); host.set((v = 2) => v); }',
    'function f(host) { host.get((v = 2) => v); host.set((v = 1) => v); }'],
];
for (const [left, right] of comparisons) assert.throws(() => verifyCallableSignatures(left, right), {name: 'AssertionError'});
for (const source of [
  'function f(x) { let held = x; x = 2; return (v = held) => v; }',
  'function f(x) { let held = x; held = 2; return (v = held) => v; }',
  'function f() { let held = external; return (v = held) => v; }',
  'function f(x) { let held = x; eval("held = 2"); return (v = held) => v; }',
  'function f() { let held = read(); return (v = held) => v; }',
]) assert.throws(() => verifyCallableSignatures(source, source), {name: 'AssertionError'});
console.log(JSON.stringify({executedMutations: changes.length, scopeCounterexamples: comparisons.length,
  alphaAndCopyEquivalence: true, unsupportedDefaultsRejected: true}));
