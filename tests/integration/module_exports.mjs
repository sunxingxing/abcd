import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const exports = await import(pathToFileURL(process.argv[2]));
assert.equal(exports.x, 2);
assert.equal(exports.y, 2);
assert.equal(exports.z, 2);
