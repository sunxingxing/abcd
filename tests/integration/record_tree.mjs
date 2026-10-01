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

for (const args of [['source', 'array'], ['source', 'array', 'late']]) {
const arraySource = execFileSync(process.argv[2], args, {encoding: 'utf8'});
assert.match(arraySource, /let answer = \[42\];/);
assert.ok(!arraySource.includes('$value'));
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
}

let anonymousExecutions = 0;
for (const profile of ['1202', '1206', '1301']) {
  for (const mode of ['anonymous', 'anonymous-array', 'anonymous-late', 'anonymous-update', 'named-default']) {
    const generated = execFileSync(process.argv[2], [mode, profile], {encoding: 'utf8'});
    const context = vm.createContext({});
    vm.runInContext('Object.defineProperty(Array.prototype, "0", {set() { throw new Error("inherited setter"); }, configurable: true});', context);
    const root = new vm.SourceTextModule(generated, {context});
    await root.link(() => { throw new Error('unexpected import'); });
    assert.throws(() => root.namespace.default, {name: 'ReferenceError'});
    await root.evaluate();
    assert.deepEqual(Object.keys(root.namespace), ['default']);
    if (mode.includes('array') || mode.includes('late')) {
      assert.match(generated, /export default \[42\];/);
      const array = root.namespace.default;
      assert.equal(array.length, 1);
      assert.equal(Object.getPrototypeOf(array), vm.runInContext('Array.prototype', context));
      assert.deepEqual(Object.getOwnPropertyDescriptor(array, '0'), {
        value: 42, writable: true, enumerable: true, configurable: true,
      });
    } else assert.equal(root.namespace.default, mode === 'anonymous-update' ? 7 : 42);
    if (mode === 'anonymous-update' || mode === 'named-default') assert.ok(!generated.includes('export default'));
    else assert.ok(!generated.includes('let $default'));
    ++anonymousExecutions;
  }
  const generated = execFileSync(process.argv[2], ['anonymous', profile], {encoding: 'utf8'});
  const mutation = generated.replace('export default 42;', 'export default 7;');
  assert.notEqual(mutation, generated);
  const root = new vm.SourceTextModule(mutation);
  await root.link(() => { throw new Error('unexpected import'); }); await root.evaluate();
  assert.notEqual(root.namespace.default, 42);
}
console.log(`default expression exports: ${anonymousExecutions} decoded ABC executions, 3 valid-syntax mutations`);
