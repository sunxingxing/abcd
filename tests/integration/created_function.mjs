import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {recovered, arrow, annotated, factory, different, preserved, reverse, captured,
       changed, siblings, separated, setter, shadowed, ancestor} =
    await import(pathToFileURL(process.argv[2]));
assert.equal(recovered.name, 'Recovered');
assert.equal(arrow.name, 'Recovered');
assert.equal(arrow.length, 1);
assert.equal(arrow(37), 37);
assert.equal(Object.hasOwn(arrow, 'prototype'), false);
assert.throws(() => new arrow(), TypeError);
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
assert.equal(captured(replacement)(), replacement);
assert.equal(changed(replacement)(), undefined);
const independent = captured(42);
assert.equal(captured(17)(), 17);
assert.equal(independent(), 42);
let getter, writer;
assert.equal(siblings((read, write) => {
  getter = read;
  writer = write;
  assert.equal(read(), undefined);
  assert.equal(write(replacement), replacement);
  assert.equal(read(), replacement);
}), replacement);
assert.equal(writer(17), 17);
assert.equal(getter(), 17);
// A reentrant factory invocation gets its own cell and cannot change ours.
siblings((read, write) => {
  write(42);
  assert.equal(getter(), 17);
  writer(replacement);
  assert.equal(read(), 42);
});
assert.equal(getter(), replacement);
separated((first, second) => {
  assert.equal(first(), true);
  assert.equal(second(), false);
});
const writeOnly = setter(42);
assert.equal(writeOnly(replacement), replacement);
assert.equal(writeOnly(undefined), undefined);
assert.equal(shadowed(replacement)(17), replacement);
assert.equal(ancestor(replacement)(), replacement);
