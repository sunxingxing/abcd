import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const array = process.argv[3] === '--array';
const source = execFileSync(process.argv[2], [array ? '--all-path-array-source' : '--all-path-source'], {encoding: 'utf8'});
assert.ok(source.trim());
if (array) assert.match(source, /\[\$arg0, \$arg0\]/);
else assert.match(source, /\["__proto__"\]: \$arg0/);
assert.doesNotMatch(source, /\.__proto__ = /);
const recovered = Function(`"use strict"; return ${source}`)();

// Independent semantics: both paths perform only inert register operations.
// The two own definitions then store the unchanged input in original order.
function reference(input) {
  const result = array ? [] : {};
  for (let index = 0; index < 2; ++index) Object.defineProperty(result,
    array ? index : '__proto__', {
      value: input, writable: true, enumerable: true, configurable: true,
    });
  return result;
}
const token = {};
const symbol = Symbol('input');
const fn = function original() {};
const values = [undefined, null, false, true, 0, -0, 1, -1, NaN,
  Infinity, -Infinity, '', 'value', 1n, symbol, token, [], fn];
let cases = 0;
for (const input of values) {
  const actual = recovered(input), expected = reference(input);
  assert.equal(Object.getPrototypeOf(actual), array ? Array.prototype : Object.prototype);
  assert.deepEqual(Reflect.ownKeys(actual), Reflect.ownKeys(expected));
  for (const key of Reflect.ownKeys(expected)) {
    const descriptor = Object.getOwnPropertyDescriptor(actual, key);
    const prior = Object.getOwnPropertyDescriptor(expected, key);
    assert.ok(Object.is(descriptor.value, prior.value));
    assert.deepEqual([descriptor.writable, descriptor.enumerable, descriptor.configurable],
      [prior.writable, prior.enumerable, prior.configurable]);
  }
  assert.notEqual(actual, recovered(input));
  ++cases;
}
const inaccessible = new Proxy({}, {get() { assert.fail('pure control observed an input property'); }});
assert.equal(array ? recovered(inaccessible)[0] : recovered(inaccessible).__proto__, inaccessible);
console.log(`${cases + 1} phased all-path ${array ? 'array' : 'object'} cases preserve identity and own descriptors`);
