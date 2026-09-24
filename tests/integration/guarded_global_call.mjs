import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';

const url = pathToFileURL(process.argv[2]);
const originalReflect = Reflect;
const originalSet = originalReflect.set;
const getterError = new Error('second prototype read');
const calls = [];
const event = Object.freeze({
  prototype: Symbol(),
  set: Symbol(),
  call: Symbol()
});
const viewBase = function ViewPU() {};
const prototype = viewBase.prototype;
const reads = [];
let throwOnSecondPrototype = false;
let prototypeReads = 0;
globalThis.ViewPU = new Proxy(viewBase, {
  get(target, key, receiver) {
    if (key === 'prototype') {
      reads.push(event.prototype);
      ++prototypeReads;
      if (throwOnSecondPrototype && prototypeReads === 2) {
        throw getterError;
      }
      return prototype;
    }
    return originalReflect.get(target, key, receiver);
  }
});
const observedReflect = new Proxy(originalReflect, {
  get(target, key, receiver) {
    if (key !== 'set') return originalReflect.get(target, key, receiver);
    reads.push(event.set);
    return function (object, key, value) {
      reads.push(event.call);
      calls.push({thisValue: this, receiver: object, key, value});
      return originalSet(object, key, value);
    };
  }
});
globalThis.Reflect = observedReflect;
try {
  delete prototype.finalizeConstruction;
  await import(`${url.href}?absent`);
  assert.deepEqual(reads, [event.prototype, event.set,
                           event.prototype, event.call]);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].thisValue, observedReflect);
  assert.equal(calls[0].receiver, prototype);
  assert.equal(calls[0].key, 'finalizeConstruction');
  assert.equal(typeof calls[0].value, 'function');
  assert.equal(prototype.finalizeConstruction, calls[0].value);
  calls.length = 0;
  reads.length = 0;
  prototypeReads = 0;
  await import(`${url.href}?present`);
  assert.deepEqual(reads, [event.prototype]);
  assert.equal(calls.length, 0);
  delete prototype.finalizeConstruction;
  reads.length = 0;
  prototypeReads = 0;
  throwOnSecondPrototype = true;
  await assert.rejects(import(`${url.href}?throwing`),
                       error => error === getterError);
  assert.deepEqual(reads, [event.prototype, event.set, event.prototype]);
  assert.equal(calls.length, 0);
} finally {
  globalThis.Reflect = originalReflect;
  delete globalThis.ViewPU;
}
