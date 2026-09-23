import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const sync = await import(pathToFileURL(process.argv[2]).href);
const async = await import(pathToFileURL(process.argv[3]).href);
assert.equal(sync.identity.name, 'identity');
assert.equal(sync.identity.length, 1);
assert.equal(sync.identity(42), 42);
assert.equal(async.identity.name, 'identity');
assert.equal(async.identity.length, 1);
assert.ok(async.identity(42) instanceof Promise);
assert.equal(await async.identity(42), 42);
