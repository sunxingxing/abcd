import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const cases = [
  ['--increment-source', '(function() { let value; try { return {["__proto__"]: (value = 1, ++value), ["__proto__"]: (value = 1, ++value)}; } catch (error) { return error; } })'],
  ['--decrement-source', '(function() { let value; try { return {["__proto__"]: (value = 1, --value), ["__proto__"]: (value = 1, --value)}; } catch (error) { return error; } })'],
  ['--numeric-source', '(function() { try { return {["__proto__"]: 1, ["__proto__"]: 1}; } catch (error) { return error; } })']
];
for (const [option, native] of cases) {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  const context = vm.createContext({});
  const prototype = vm.runInContext('Object.prototype', context);
  Object.defineProperty(prototype, '__proto__', {configurable: true, set() { assert.fail('own definition invoked setter'); }});
  const generated = vm.runInContext(source, context), reference = vm.runInContext(native, context);
  const argument = new Proxy({}, {get() { assert.fail('constant initializer read its unused argument'); }});
  const before = generated(argument), after = generated(argument), expected = reference(argument);
  assert.notEqual(before, after);
  for (const value of [before, after]) {
    assert.equal(Object.getPrototypeOf(value), prototype);
    assert.deepEqual(Object.getOwnPropertyDescriptors(value), Object.getOwnPropertyDescriptors(expected));
    assert.deepEqual(Object.keys(value), ['__proto__']);
  }
}
console.log('Three native unary initializer forms passed grouping, own-property and fresh-allocation checks');

const references = [
  () => typeof 9, () => typeof 9, () => +9, () => 9, () => -9, () => ~9,
  () => { let value = 9; return ++value; }, () => { let value = 9; return --value; },
  () => !!9, () => !9, () => +7, () => 7, () => ~7,
  () => { let value = 7; return ++value; }, () => { let value = 7; return --value; }
];
assert.ok(process.argv[3]);
for (const [index, reference] of references.entries()) {
  const source = execFileSync(process.argv[3], ['--source', String(index)], {encoding: 'utf8'});
  const fn = vm.runInNewContext(source);
  const result = fn(), second = fn();
  assert.notEqual(result, second);
  assert.deepEqual(Object.keys(result), ['field0', 'field1']);
  for (const key of ['field0', 'field1']) {
    assert.ok(Object.is(result[key], reference()));
    const descriptor = Object.getOwnPropertyDescriptor(result, key);
    assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
  }
}
console.log('Fifteen normal unary encodings passed independent acc/register operand and result checks');
