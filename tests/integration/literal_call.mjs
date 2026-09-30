import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const source = execFileSync(process.argv[2], ['--late-call-source'], {encoding: 'utf8'});
const generated = vm.runInNewContext(source);
const reference = value => ({field0: value.method.bind(value), field1: value.method.bind(value)});
let scenarios = 0;
function run(fn, fail, native) {
  const events = [], results = [];
  let step = 0, reads = 0;
  const event = (name, id) => {
    events.push([name, id]);
    if (++step === fail) throw new Error(`step-${step}`);
  };
  const receiver = {
    get method() {
      assert.equal(this, receiver);
      const id = ++reads;
      event('method', id);
      const callable = function() { assert.equal(this, receiver); return id; };
      Object.defineProperty(callable, 'bind', {get() {
        assert.equal(this, callable); event('bind-get', id);
        return function(value) {
          assert.equal(this, callable); assert.equal(value, receiver);
          event('bind-call', id);
          const result = native ? Function.prototype.bind.call(callable, value) : {id};
          results.push(result); return result;
        };
      }});
      return callable;
    },
  };
  try {
    const result = fn(receiver);
    assert.deepEqual(Object.keys(result), ['field0', 'field1']);
    for (const [i, key] of ['field0', 'field1'].entries()) {
      assert.equal(result[key], results[i]);
      const descriptor = Object.getOwnPropertyDescriptor(result, key);
      assert.ok(descriptor.writable && descriptor.configurable && descriptor.enumerable);
      if (native) assert.equal(result[key].call({wrong: true}), i + 1);
    }
    events.push(['return']);
  } catch (error) {
    if (error.code === 'ERR_ASSERTION') throw error;
    events.push(['throw', error.message]);
  }
  return events;
}
for (const native of [false, true]) for (let fail = 0; fail <= 6; ++fail) {
  assert.deepEqual(run(generated, fail, native), run(reference, fail, native));
  ++scenarios;
}
console.log(`Literal member calls: ${scenarios} behavior scenarios passed`);
