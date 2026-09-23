import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const record = await import(pathToFileURL(process.argv[2]));
const dependency = await import(pathToFileURL(process.argv[3]));
assert.equal(record.copied, 7);
dependency.setValue(9);
assert.equal(dependency.value, 9);
assert.equal(record.copied, 7);
