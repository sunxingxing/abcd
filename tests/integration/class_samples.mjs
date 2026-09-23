import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
let count = 0;
let fieldClasses = 0;
for (const filename of process.argv.slice(3)) {
  const module = await import(pathToFileURL(filename));
  for (const factory of Object.values(module)) {
    const C = factory();
    assert.notEqual(C, factory());
    assert.equal(Object.getPrototypeOf(C), Function.prototype);
    assert.equal(Object.getPrototypeOf(new C()), C.prototype);
    assert.equal(C.prototype.constructor, C);
    assert.throws(() => C(), TypeError);
    const first = Object.freeze({first: true});
    const second = Object.freeze({second: true});
    if (C.name === 'IconInfo') {
      const instance = new C(first, second);
      assert.equal(instance.getInfo(), first);
      assert.equal(instance.getIcon(), second);
    }
    if (C.name === 'RecommendProduct') {
      const instance = new C(first, second);
      assert.equal(instance.getImage(), first);
      assert.equal(instance.getIsLive(), second);
    }
    if (C.name === 'CreateFormData') {
      const fields = ['formId', 'text_key', 'image_key'];
      const instance = new C();
      assert.deepEqual(Object.keys(instance), fields);
      for (const field of fields) assert.deepEqual(
        Object.getOwnPropertyDescriptor(instance, field),
        {value: '', writable: true, enumerable: true, configurable: true});
      const writes = [];
      for (const field of fields) Object.defineProperty(C.prototype, field, {
        set(value) { writes.push({field, value, receiver: this}); }
      });
      const withSetters = new C();
      assert.deepEqual(Object.keys(withSetters), []);
      assert.deepEqual(writes.map(({field, value}) => [field, value]),
        fields.map(field => [field, '']));
      assert(writes.every(write => write.receiver === withSetters));
      fieldClasses++;
    }
    count++;
  }
}
assert.equal(count, Number(process.argv[2]));
assert.equal(fieldClasses, 1);
console.log(JSON.stringify({classes: count, fieldClasses}));
