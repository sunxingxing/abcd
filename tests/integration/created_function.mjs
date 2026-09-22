import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {recovered, annotated, factory, different, preserved, reverse} = await import(pathToFileURL(process.argv[2]));
assert.equal(recovered.name, 'Recovered');
assert.equal(recovered.length, 1);
for (const value of [undefined, null, true, 0, -0, NaN, 42n,
                     Symbol('argument'), Object.freeze({value: 1})]) {
  assert.ok(Object.is(recovered(value), value));
}
assert.equal(recovered(), undefined);
assert.equal(Object.getPrototypeOf(new recovered()), recovered.prototype);
const replacement = Object.freeze({replacement: true});
assert.equal(new recovered(replacement), replacement);
assert.equal(annotated.length, 3);
assert.equal(annotated('first', 'second', replacement), replacement);
assert.equal(annotated.call(replacement, 1, 2, 3), 3);
assert.equal(annotated('first'), undefined);
const first = factory();
const second = factory();
assert.notEqual(first, second);
assert.notEqual(first.prototype, second.prototype);
assert.equal(first.name, 'Recovered');
assert.equal(first.length, 1);
assert.equal(first(replacement), replacement);
assert.equal(second(-0), -0);
assert.equal((new factory())(42), 42);
assert.equal(different(), false);
assert.equal(preserved(replacement), replacement);
assert.equal(reverse().name, 'Other');
assert.equal(reverse()(42), undefined);
