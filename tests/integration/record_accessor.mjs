import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const {Accessor} = await import(pathToFileURL(process.argv[2]));
const descriptor = Object.getOwnPropertyDescriptor(Accessor.prototype, 'value');
assert.equal(descriptor.enumerable, false);
assert.equal(descriptor.configurable, true);
assert.equal(descriptor.get.name, 'get value');
assert.equal(descriptor.set.name, 'set value');
assert.equal(descriptor.get.length, 0);
assert.equal(descriptor.set.length, 1);
assert.equal(new Accessor().value, undefined);
assert.equal(new Accessor().read(), undefined);
