import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const executable = process.argv[2];
const emit = mode => execFileSync(executable, [mode], {
  encoding: 'utf8', timeout: 30_000, maxBuffer: 4 * 1024 * 1024,
});
const cases = emit('--names').trim().split('\n').map(line => JSON.parse(line));
assert.equal(cases.length, 648);
const kinds = new Map([
  ['function', {constructable: true}], ['arrow', {}],
  ['async-function', {async: true}], ['async-arrow', {async: true}],
  ['generator', {generator: true}], ['async-generator', {async: true, generator: true}],
]);
for (const test of cases) {
  const context = vm.createContext({});
  const factory = vm.runInContext(test.source, context);
  const fn = factory();
  assert.equal(typeof fn, 'function');
  assert.equal(fn.name, test.wanted, JSON.stringify(test));
  assert.deepEqual(Object.getOwnPropertyDescriptor(fn, 'name'), {
    value: test.wanted, writable: false, enumerable: false, configurable: true,
  });
  assert.equal(fn.length, 1);
  assert(kinds.has(test.callable));
  const {generator = false, async = false, constructable = false} = kinds.get(test.callable);
  assert.equal(Object.hasOwn(fn, 'prototype'), constructable || generator);
  if (constructable) {
    const value = {};
    assert.equal(Reflect.construct(fn, [value]), value);
  } else assert.throws(() => Reflect.construct(fn, []), TypeError);
  const value = {};
  const result = Reflect.apply(fn, {hostileReceiver: true}, [value]);
  if (generator) {
    const first = async ? await result.next() : result.next();
    assert.equal(first.value, value); assert.equal(first.done, false);
    const sent = {};
    const last = async ? await result.next(sent) : result.next(sent);
    assert.equal(last.value, sent); assert.equal(last.done, true);
    const throwing = fn(value), error = {};
    if (async) {
      await throwing.next(); await assert.rejects(throwing.throw(error), e => e === error);
    } else {
      throwing.next(); assert.throws(() => throwing.throw(error), e => e === error);
    }
  } else {
    assert.equal(async ? await result : result, value);
    if (async) {
      const error = {};
      await assert.rejects(fn({then(_, reject) { reject(error); }}), e => e === error);
    }
  }
  assert.notEqual(factory(), fn);
  Object.defineProperty(fn, 'name', {value: 'changed'});
  assert.equal(fn.name, 'changed');
}

// NamedEvaluation changes the name property without introducing an immutable
// inner binding. The recovered function must still see the outer mutation.
const shadowSource = emit('--name-shadow');
const recovered = vm.runInNewContext(shadowSource)();
const reference = (() => {
  let chosen = function(arg) { return chosen; };
  const saved = chosen; chosen = 42; return saved;
})();
assert.equal(recovered.name, reference.name);
assert.equal(recovered(), reference());
assert.equal(recovered(), 42);
const scaled = emit('--name-scale').trim().split('\n').map(line => JSON.parse(line));
assert.deepEqual(scaled.map(test => test.count), [1, 200, 201, 402]);
assert(scaled[2].work > scaled[1].work && scaled[2].work < 2 * scaled[1].work);
assert(scaled[3].work < 3 * scaled[2].work);
for (const test of scaled) {
  const factory = vm.runInNewContext(test.source);
  const functions = factory(), others = factory();
  assert.equal(functions.length, test.count);
  for (const [index, fn] of functions.entries()) {
    assert.equal(fn.name, ''); assert.equal(fn.length, 1);
    assert.equal(fn(index), index); assert.notEqual(fn, others[index]);
  }
  console.log(`native-name family count=${test.count} work=${test.work} allocated=${test.allocated}`);
}
console.log(`${cases.length} native-name contexts and mutable capture verified`);
