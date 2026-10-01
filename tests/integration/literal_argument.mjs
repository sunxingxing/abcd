import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['--argument-source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE'));
const context = vm.createContext({});
const recovered = vm.runInContext(source, context);
const reference = vm.runInContext(`(Ctor, arg) => ({
  field0: new Ctor({value: arg.value}, [9], {}).value,
  field1: new Ctor({value: arg.value}, [9], {}).value
})`, context);
const objectPrototype = vm.runInContext('Object.prototype', context);
const arrayPrototype = vm.runInContext('Array.prototype', context);
function run(fn, token, fail, replace, proxy) {
  const events = [], payloads = [], seen = new Set();
  const thrown = {};
  let step = 0, reads = 0, calls = 0;
  const event = (kind, index) => {
    events.push([kind, index]); if (++step === fail) throw thrown;
  };
  const arg = {get value() { event('argument-getter', ++reads); return token; }};
  function Target(one, array, empty) {
    assert.equal(new.target, Ctor);
    for (const value of [one, array, empty]) {
      assert.ok(!seen.has(value)); seen.add(value);
    }
    assert.equal(Object.getPrototypeOf(one), objectPrototype);
    assert.equal(Object.getPrototypeOf(array), arrayPrototype);
    assert.equal(Object.getPrototypeOf(empty), objectPrototype);
    assert.deepEqual(Object.keys(one), ['value']); assert.equal(one.value, token);
    assert.ok(Array.isArray(array)); assert.equal(array.length, 1); assert.equal(array[0], 9);
    assert.deepEqual(Object.keys(empty), []);
    for (const [object, key] of [[one, 'value'], [array, '0']]) {
      const descriptor = Object.getOwnPropertyDescriptor(object, key);
      assert.ok(descriptor.writable && descriptor.enumerable && descriptor.configurable);
    }
    const index = ++calls; event('constructor', index);
    const payload = {one, array, empty}; payloads.push(payload);
    // The constructor observes and retains each freshly initialized argument.
    one.value = empty; array.push(one);
    const receiver = replace ? {} : this;
    Object.defineProperty(receiver, 'value', {get() {
      assert.equal(this, receiver); event('result-getter', index); return payload;
    }});
    return replace ? receiver : 42;
  }
  const Ctor = proxy ? new Proxy(Target, {construct(target, args, newTarget) {
    assert.equal(target, Target); assert.equal(newTarget, Ctor); assert.equal(args.length, 3);
    event('construct-trap', calls + 1); return Reflect.construct(target, args, newTarget);
  }}) : Target;
  try {
    const result = fn(Ctor, arg);
    assert.deepEqual(Object.keys(result), ['field0', 'field1']);
    for (const [index, key] of ['field0', 'field1'].entries()) {
      assert.equal(result[key], payloads[index]);
      assert.equal(result[key].one.value, result[key].empty);
      assert.equal(result[key].array[1], result[key].one);
    }
    events.push(['return']);
  } catch (error) {
    if (error !== thrown) throw error;
    events.push(['throw']);
  }
  return events;
}
let scenarios = 0;
for (const token of [undefined, null, false, -0, NaN, 1n, Symbol(), {}])
  for (const replace of [false, true]) for (const proxy of [false, true]) for (let fail = 0; fail <= 8; ++fail) {
    assert.deepEqual(run(recovered, token, fail, replace, proxy), run(reference, token, fail, replace, proxy));
    ++scenarios;
  }
for (const bad of [undefined, null, false, 1, {}, () => {}]) {
  let reads = 0;
  assert.throws(() => recovered(bad, {get value() { ++reads; return 1; }}), error => error.name === 'TypeError');
  assert.equal(reads, 1);
}
console.log(`Literal aggregate arguments: ${scenarios} effect/identity scenarios and 6 argument-before-TypeError checks passed.`);
