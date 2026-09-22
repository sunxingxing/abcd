import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const classes = await import(pathToFileURL(process.argv[2]));
for (const count of [200, 201, 400, 800, 1022, 1023, 1600]) {
  const C = classes[`members${count}`];
  assert.equal(C.name, 'ClassValue');
  assert.equal(C.length, 0);
  assert.deepEqual(Reflect.ownKeys(C.prototype), [
    'constructor', ...Array.from({length: count}, (_, i) => `m${i}`)
  ]);
  for (let i = 0; i < count; ++i) {
    const method = C.prototype[`m${i}`];
    assert.equal(method.name, `m${i}`);
    assert.equal(method.length, 0);
    assert.equal(method(), method);
    assert.deepEqual(Object.getOwnPropertyDescriptor(C.prototype, `m${i}`), {
      value: method, writable: true, enumerable: false, configurable: true
    });
  }
}
