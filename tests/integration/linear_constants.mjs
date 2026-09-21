import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {recovered, named0, named1} = await import(pathToFileURL(process.argv[2]).href);
const text = recovered();
assert.equal(text.length, 5);
assert.deepEqual(Array.from({length: text.length}, (_, i) => text.charCodeAt(i)),
  [0x41, 0, 0xd800, 0x22, 0x5c]);
for (const read of [named0, named1]) {
  assert.equal(read({[text]: 42}), 42);
  assert.equal(read({}), undefined);
  assert.throws(() => read(null), TypeError);
  const object = Object.create({get [text]() {return this.value;}});
  object.value = 19;
  assert.equal(read(object), 19);
  const error = {};
  const proxy = new Proxy({}, {get(target, key, receiver) {
    assert.equal(key, text);
    assert.equal(receiver, proxy);
    throw error;
  }});
  assert.throws(() => read(proxy), value => value === error);
}
