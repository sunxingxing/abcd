import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

const source = execFileSync(process.argv[2], ['--lexical-source'], {encoding: 'utf8'});
assert.ok(source.includes('try {') && !source.includes('INCOMPLETE'));
const generated = runInNewContext(source);
function reference(input) {
  let local = input;
  try {
    return {['__proto__']: {['__proto__']: local}, ['__proto__']: {['__proto__']: local}};
  } catch (error) { return error; }
}
const hostile = new Proxy({}, {get() { assert.fail('cell read must not access the payload'); }});
const inputs = [undefined, null, false, true, 0, -0, NaN, Infinity, 1n, 'text', Symbol('cell'), {}, [], () => {}, hostile];
function inspect(fn, input) {
  const first = fn(input), second = fn(input);
  assert.notEqual(first, second);
  assert.notEqual(first.__proto__, second.__proto__);
  for (const outer of [first, second]) {
    for (const object of [outer, outer.__proto__]) {
      assert.deepEqual(Object.keys(object), ['__proto__']);
      const descriptor = Object.getOwnPropertyDescriptor(object, '__proto__');
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
      assert.notEqual(Object.getPrototypeOf(object), descriptor.value);
    }
    assert.ok(Object.is(outer.__proto__.__proto__, input));
  }
}
for (const input of inputs) { inspect(reference, input); inspect(generated, input); }
console.log(`Phased initialized cell literal: ${inputs.length} payloads, fresh identities and descriptors passed`);
