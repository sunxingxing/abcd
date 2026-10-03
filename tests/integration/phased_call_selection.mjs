import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const tool = process.argv[2];
const token = {}, failure = {}, symbol = Symbol('argument');
const hostile = {[Symbol.toPrimitive]() { assert.fail('condition coerced'); }};
const flags = [undefined, null, false, 0, -0, NaN, '', true, 1, 1n, symbol, hostile];
const options = [undefined, null, false, 0, -0, NaN, 'argument', symbol, token];
let cases = 0;
for (const computed of [false, true]) for (const count of [1, 2]) {
  const source = execFileSync(tool, [computed ? '--phased-computed-call-selection-source' :
    '--phased-call-selection-source', ...(count === 2 ? ['--twice'] : [])], {encoding: 'utf8'});
  assert.ok(source.includes('value.method(') && source.includes(' ? '));
  assert.ok(!source.includes('.call(') && !source.includes('INCOMPLETE'));
  const recovered = Function(`"use strict"; return ${source}`)();
  const reference = (value, flag) => {
    try {
      let result;
      for (let i = 0; i < count; ++i) result = value.method(flag ? 7 : computed ? value.option : 448);
      return result;
    } catch (error) { return error; }
  };
  function run(fn, flag, option, replace, failAt, noncallable) {
    const trace = []; let step = 0, version = 0;
    const event = (kind, ...args) => {
      trace.push([kind, ...args]);
      if (++step === failAt) throw failure;
    };
    const value = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, value);
      if (key === 'method') {
        event('get-method', version);
        const capturedVersion = version;
        if (noncallable && !version) return null;
        const method = function(argument) {
          assert.equal(this, value);
          assert.ok(Object.is(argument, flag ? 7 : computed ? option : 448));
          event('call', capturedVersion, argument);
          return token;
        };
        Object.defineProperty(method, 'call', {get() { assert.fail('mutable .call helper'); }});
        return method;
      }
      assert.equal(key, 'option'); event('get-option');
      if (replace) ++version;
      return option;
    }});
    // The original bytecode handler returns the exception; an escaping throw
    // is a failure, not an interchangeable outcome in this harness.
    const result = fn(value, flag);
    assert.ok(result === token || result === failure || (noncallable && result instanceof TypeError));
    return {trace, outcome: result === token ? 'return' : result === failure ? 'sentinel' : 'TypeError'};
  }
  for (const flag of flags) for (const option of options) for (const replace of [false, true])
    for (const failAt of [0, 1, 2, 3, 4, 5, 6]) for (const noncallable of [false, true]) {
      assert.deepEqual(run(recovered, flag, option, replace, failAt, noncallable),
        run(reference, flag, option, replace, failAt, noncallable));
      ++cases;
    }
}
console.log(`${cases} phased conditional call receiver/order/exception scenarios passed`);
