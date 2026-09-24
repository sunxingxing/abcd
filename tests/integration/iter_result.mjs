import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const source = await import(pathToFileURL(process.argv[2]));
for (const value of [undefined, null, 0, 'text', Symbol('x'), {}]) {
  for (const done of [false, true]) {
    const result = source.result(value, done);
    assert.ok(Object.is(result.value, value));
    assert.equal(result.done, done);
    assert.equal(Object.getPrototypeOf(result), Object.prototype);
    assert.deepEqual(Object.keys(result), ['value', 'done']);
    for (const key of ['value', 'done']) {
      const descriptor = Object.getOwnPropertyDescriptor(result, key);
      assert.equal(descriptor.writable, true);
      assert.equal(descriptor.enumerable, true);
      assert.equal(descriptor.configurable, true);
    }
  }
}
