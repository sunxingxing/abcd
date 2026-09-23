import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const record = await import(pathToFileURL(process.argv[2]));
const dependency = await import(pathToFileURL(process.argv[3]));
assert.equal(record.copied, 7);
const derived = new record.Derived(2, 'y');
assert.deepEqual(derived.values, [2, 'y']);
assert.equal(derived.target, record.Derived);
assert.equal(record.Derived.length, 0);
assert.equal(Object.getPrototypeOf(record.Derived), dependency.Base);
dependency.setValue(9);
assert.equal(dependency.value, 9);
assert.equal(record.copied, 7);
