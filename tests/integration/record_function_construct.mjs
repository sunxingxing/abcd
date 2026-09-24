import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const recovered = await import(pathToFileURL(process.argv[2]));
const instance = recovered.identity;
assert.equal(typeof instance, 'object');
assert.equal(instance.constructor.name, 'identity');
assert.equal(Object.getPrototypeOf(instance), instance.constructor.prototype);
assert.deepEqual(Object.keys(instance), []);
