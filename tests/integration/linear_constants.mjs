import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {recovered} = await import(pathToFileURL(process.argv[2]).href);
const text = recovered();
assert.equal(text.length, 5);
assert.deepEqual(Array.from({length: text.length}, (_, i) => text.charCodeAt(i)),
  [0x41, 0, 0xd800, 0x22, 0x5c]);
