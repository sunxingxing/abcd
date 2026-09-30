import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['source'], {encoding: 'utf8'});
const module = new vm.SourceTextModule(source);
await module.link(() => { throw new Error('unexpected import'); });
assert.throws(() => module.namespace.answer, {name: 'ReferenceError'});
await module.evaluate();
assert.equal(module.namespace.answer, 42);
assert.deepEqual(Object.keys(module.namespace), ['answer']);

const arraySource = execFileSync(process.argv[2], ['source', 'array'], {encoding: 'utf8'});
const arrayContext = vm.createContext({});
vm.runInContext('Object.defineProperty(Array.prototype, "0", {set() { throw new Error("inherited setter"); }, configurable: true});', arrayContext);
const arrayModule = new vm.SourceTextModule(arraySource, {context: arrayContext});
await arrayModule.link(() => { throw new Error('unexpected import'); });
await arrayModule.evaluate();
const array = arrayModule.namespace.answer;
assert.equal(Array.isArray(array), true);
assert.equal(array.length, 1);
assert.deepEqual(Object.getOwnPropertyDescriptor(array, '0'), {
  value: 42, writable: true, enumerable: true, configurable: true,
});
assert.equal(Object.getPrototypeOf(array), vm.runInContext('Array.prototype', arrayContext));
