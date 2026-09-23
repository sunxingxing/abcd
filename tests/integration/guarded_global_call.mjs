import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const url = pathToFileURL(process.argv[2]);
globalThis.ViewPU = function ViewPU() {};
const calls = [];
const originalSet = Reflect.set;
Reflect.set = function (receiver, key, value) {
  calls.push({receiver, key, value});
  return originalSet(receiver, key, value);
};
try {
  delete ViewPU.prototype.finalizeConstruction;
  await import(`${url.href}?absent`);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].receiver, ViewPU.prototype);
  assert.equal(calls[0].key, 'finalizeConstruction');
  assert.equal(typeof calls[0].value, 'function');
  assert.equal(ViewPU.prototype.finalizeConstruction, calls[0].value);
  calls.length = 0;
  await import(`${url.href}?present`);
  assert.equal(calls.length, 0);
} finally {
  Reflect.set = originalSet;
  delete globalThis.ViewPU;
}
