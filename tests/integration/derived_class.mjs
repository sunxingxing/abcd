import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {Derived, GlobalDerived, PositionalDerived} =
  await import(pathToFileURL(process.argv[2]));
const instance = new Derived(3, 'x');
assert.deepEqual(instance.values, [3, 'x']);
assert.equal(Derived.length, 0);
assert.equal(Derived.prototype.constructor, Derived);
assert.equal(Object.getPrototypeOf(Derived.prototype),
             Object.getPrototypeOf(Derived).prototype);
const globalInstance = new GlobalDerived(4, 'y');
assert.deepEqual(globalInstance.values, [4, 'y']);
assert.equal(GlobalDerived.length, 0);
assert.equal(GlobalDerived.prototype.constructor, GlobalDerived);
assert.equal(Object.getPrototypeOf(GlobalDerived.prototype),
             Object.getPrototypeOf(GlobalDerived).prototype);
assert.equal(PositionalDerived.length, 2);
assert.deepEqual(new PositionalDerived(3, 'x', 'ignored').values, [3, 'x']);
assert.deepEqual(new PositionalDerived(3).values, [3, undefined]);
function Alternate() {}
const alternate = Reflect.construct(PositionalDerived, [9, 'z'], Alternate);
assert.deepEqual(alternate.values, [9, 'z']);
assert.equal(Object.getPrototypeOf(alternate), Alternate.prototype);
