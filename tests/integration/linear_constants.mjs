import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {recovered, named0, named1, write0, write1, writeCollision} = await import(pathToFileURL(process.argv[2]).href);
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
for (const emitted of [write0, write1, writeCollision]) {
  // Reparse outside module strict mode: the generated write must enforce it.
  const sloppy = Function(`return (${emitted.toString()});`)();
  for (const write of [emitted, sloppy]) {
    const object = {};
    for (const value of [undefined, null, -0, NaN, 2n, {}, Symbol()]) {
      assert.equal(write(object, value), value);
      assert.equal(object[text], value);
    }
    const marker = {};
    let calls = 0;
    const inherited = Object.create({set [text](value) {
      assert.equal(this, inherited);
      assert.equal(value, marker);
      ++calls;
      return 99;
    }});
    assert.equal(write(inherited, marker), marker);
    assert.equal(calls, 1);
    assert.equal(Object.hasOwn(inherited, text), false);
    const proxy = new Proxy({}, {set(target, key, value, receiver) {
      assert.equal(key, text);
      assert.equal(value, marker);
      assert.equal(receiver, proxy);
      ++calls;
      return true;
    }});
    assert.equal(write(proxy, marker), marker);
    assert.equal(calls, 2);
    const child = Object.create(new Proxy({}, {set(target, key, value, receiver) {
      assert.equal(key, text);
      assert.equal(value, marker);
      assert.equal(receiver, child);
      return true;
    }}));
    assert.equal(write(child, marker), marker);
    Object.defineProperty(Number.prototype, text, {configurable: true, set(value) {
      assert.equal(this, 7);
      assert.equal(value, marker);
      ++calls;
    }});
    try {
      assert.equal(write(7, marker), marker);
      assert.equal(calls, 3);
    } finally {
      delete Number.prototype[text];
    }
    const denied = new Proxy({}, {set() {return false;}});
    assert.throws(() => write(denied, marker), TypeError);
    const error = {};
    const throwing = new Proxy({}, {set() {throw error;}});
    assert.throws(() => write(throwing, marker), value => value === error);
    for (const receiver of [null, undefined, 1, true, 'text', 2n, Symbol(),
      Object.freeze({[text]: 1}), Object.preventExtensions({}),
      Object.create({get [text]() {return 1;}})]) {
      assert.throws(() => write(receiver, marker), TypeError);
    }
  }
}
