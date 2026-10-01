import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'});
assert.ok(!source.includes('INCOMPLETE'));
const recovered = vm.runInNewContext(source);
const reference = (Ctor, arg) => ({field0: new Ctor(arg).value, field1: new Ctor(arg).value});
function run(fn, fail, replace, proxy) {
  const events = [], values = [];
  const arg = {}, thrown = {};
  let step = 0, id = 0;
  const event = (kind, index) => {
    events.push([kind, index]);
    if (++step === fail) throw thrown;
  };
  function Target(value) {
    assert.equal(value, arg);
    assert.equal(new.target, Ctor);
    const index = ++id;
    event('constructor', index);
    const receiver = replace ? {} : this;
    const payload = {index}; values.push(payload);
    Object.defineProperty(receiver, 'value', {get() {
      assert.equal(this, receiver);
      event('getter', index); return payload;
    }});
    // A primitive return preserves this; an object replaces it.
    return replace ? receiver : 42;
  }
  const Ctor = proxy ? new Proxy(Target, {construct(target, args, newTarget) {
    assert.equal(target, Target); assert.equal(newTarget, Ctor);
    assert.equal(args.length, 1); assert.equal(args[0], arg);
    event('construct-trap', id + 1);
    return Reflect.construct(target, args, newTarget);
  }}) : Target;
  try {
    const result = fn(Ctor, arg);
    assert.deepEqual(Object.keys(result), ['field0', 'field1']);
    for (const [index, key] of ['field0', 'field1'].entries()) {
      assert.equal(result[key], values[index]);
      const descriptor = Object.getOwnPropertyDescriptor(result, key);
      assert.ok(descriptor.writable && descriptor.configurable && descriptor.enumerable);
    }
    events.push(['return']);
  } catch (error) {
    if (error !== thrown) throw error;
    events.push(['throw']);
  }
  return events;
}
let scenarios = 0;
for (const replace of [false, true]) for (const proxy of [false, true]) for (let fail = 0; fail <= 6; ++fail) {
  assert.deepEqual(run(recovered, fail, replace, proxy), run(reference, fail, replace, proxy));
  ++scenarios;
}
for (const bad of [undefined, null, false, 1, {}, () => {}])
  assert.throws(() => recovered(bad, {}), error => error.name === 'TypeError');
console.log(`Literal construction: ${scenarios} effect/identity scenarios and 6 non-constructible values passed.`);
