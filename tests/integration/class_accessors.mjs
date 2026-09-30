import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const source = execFileSync(process.argv[2], [process.argv[3], '--accessors'], {encoding: 'utf8'});
const Accessors = vm.runInNewContext(source);
assert.deepEqual(Object.getOwnPropertyNames(Accessors.prototype), ['constructor', 'navPageInfos', 'windowUtil']);
for (const key of ['navPageInfos', 'windowUtil']) {
  const descriptor = Object.getOwnPropertyDescriptor(Accessors.prototype, key);
  assert.equal(descriptor.enumerable, false);
  assert.equal(descriptor.configurable, true);
  assert.equal(descriptor.get.name, `get ${key}`);
  assert.equal(descriptor.set.name, `set ${key}`);
  assert.equal(descriptor.get.length, 0);
  assert.equal(descriptor.set.length, 1);
  assert.equal(Object.hasOwn(descriptor.get, 'prototype'), false);
  assert.equal(Object.hasOwn(descriptor.set, 'prototype'), false);
  assert.throws(() => Reflect.construct(descriptor.get, []), {name: 'TypeError'});
  assert.throws(() => Reflect.construct(descriptor.set, []), {name: 'TypeError'});
  for (const kind of ['get', 'set']) {
    for (const failureAt of [null, 'base', 'lookup', 'call']) {
      const trace = [], sentinel = {}, input = {}, output = {};
      let backing;
      backing = new Proxy({}, {get(_target, name) {
        trace.push(['lookup', name]); assert.equal(name, kind);
        if (failureAt === 'lookup') throw sentinel;
        return function(...args) {
          trace.push(['call', args.length]); assert.equal(this, backing);
          if (kind === 'set') assert.equal(args[0], input);
          if (failureAt === 'call') throw sentinel;
          return output;
        };
      }});
      const receiver = new Proxy({}, {get(_target, name) {
        trace.push(['base', name]); assert.equal(name, `__${key}`);
        if (failureAt === 'base') throw sentinel;
        return backing;
      }});
      const invoke = () => Reflect.apply(descriptor[kind], receiver, kind === 'get' ? [] : [input]);
      if (failureAt) assert.throws(invoke, error => error === sentinel);
      else assert.equal(invoke(), kind === 'get' ? output : undefined);
      const expected = [['base', `__${key}`], ['lookup', kind], ['call', kind === 'get' ? 0 : 1]];
      assert.deepEqual(trace, expected.slice(0, failureAt === 'base' ? 1 : failureAt === 'lookup' ? 2 : 3));
    }
  }
}
console.log('Actual ABC accessor bodies preserve native descriptors, names, receiver and abrupt completion');

// A real ordinary static member prepared through the same class owner path.
const ordinarySource = execFileSync(process.argv[2], [process.argv[3], '--static-method'], {encoding: 'utf8', timeout: 60_000});
const Ordinary = vm.runInNewContext(ordinarySource);
assert.equal(Ordinary.getEntryName(), 'Index');
const ordinary = Object.getOwnPropertyDescriptor(Ordinary, 'getEntryName');
assert.equal(ordinary.enumerable, false);
assert.equal(ordinary.configurable, true);
assert.equal(ordinary.writable, true);
assert.equal(ordinary.value.name, 'getEntryName');
assert.equal(ordinary.value.length, 0);
assert.equal(Object.hasOwn(ordinary.value, 'prototype'), false);
assert.throws(() => new ordinary.value(), e => e.name === 'TypeError');
