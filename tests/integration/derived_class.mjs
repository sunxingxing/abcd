import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {Derived} = await import(pathToFileURL(process.argv[2]));
const instance = new Derived(3, 'x');
assert.deepEqual(instance.values, [3, 'x']);
assert.equal(Derived.length, 0);
assert.equal(Derived.prototype.constructor, Derived);
assert.equal(Object.getPrototypeOf(Derived.prototype),
             Object.getPrototypeOf(Derived).prototype);
