import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['--buffer-source'], {encoding: 'utf8'});
const context = vm.createContext({});
vm.runInContext('Object.defineProperty(Object.prototype, "field1", {set() { throw new Error("inherited setter"); }, configurable: true});', context);
const recovered = vm.runInContext(`[${source}]`, context);
const prototype = vm.runInContext('Object.prototype', context);
let index = 0;
for (const count of [2, 200, 201, 402]) for (const mode of [0, 1, 2]) {
  // Independent model: create every buffered own key first, then execute each
  // original own definition. No recovered tree/placement table supplies answers.
  const expected = {};
  const key = i => i === 0 ? '__proto__' : `field${i}`;
  const define = (i, value) => Object.defineProperty(expected, key(i), {
    value, enumerable: true, writable: true, configurable: true,
  });
  for (let i = 0; i < count; ++i) define(i, null);
  if (mode === 2) define(0, null);
  for (let i = 0; i < count; ++i) define(mode === 1 ? count - 1 - i : i, i + 10);
  const actual = recovered[index++]();
  assert.equal(Object.getPrototypeOf(actual), prototype);
  assert.deepEqual(Reflect.ownKeys(actual), Reflect.ownKeys(expected));
  for (const name of Reflect.ownKeys(expected))
    assert.deepEqual(Object.getOwnPropertyDescriptor(actual, name),
      Object.getOwnPropertyDescriptor(expected, name));
}
assert.equal(recovered.length, index);
