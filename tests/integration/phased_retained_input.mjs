import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const values = [undefined, null, false, true, 0, -0, NaN, '', 'text', 1n, Symbol('value'), {}, []];
let cases = 0;
for (const mode of ['input', 'getter', 'prefix', 'literal-args']) for (const count of [1, 2]) {
  const source = execFileSync(process.argv[2], [`--phased-retained-${mode}-source`,
    ...(count === 2 ? ['--twice'] : [])], {encoding: 'utf8'});
  assert.ok(source.includes('try {') && source.includes('value.method(') && !source.includes('INCOMPLETE'));
  const recovered = Function(`"use strict"; return ${source}`)();
  const reference = (value, flag) => {
    try {
      let result;
      for (let i = 0; i < count; ++i) {
        if (mode === 'prefix') {
          let kept;
          result = value.method((kept = value.option, kept ? 7 : kept));
        } else {
          const kept = mode === 'getter' ? value.option : flag;
          result = mode === 'literal-args' ? value.method('on', kept ? 7 : kept) : value.method(kept ? 7 : kept);
        }
      }
      return result;
    } catch (error) {return error;}
  };
  function observe(fn, payload, mutate, failAt, noncallable) {
    const trace = [], failure = {}, token = {}; let step = 0, version = 0;
    const event = (...parts) => {trace.push(parts); if (++step === failAt) throw failure;};
    const value = {get option() {event('option'); return payload;}, get method() {
      event('method', version);
      if (mutate) Object.defineProperty(value, 'option', {configurable: true, get() {
        event('mutated-option'); return payload;
      }});
      const captured = version++;
      if (noncallable) return null;
      const method = function(...args) {
        assert.equal(this, value);
        if (mode === 'literal-args') assert.equal(args.shift(), 'on');
        assert.equal(args.length, 1); const arg = args[0];
        assert.ok(Object.is(arg, payload ? 7 : payload)); event('call', captured, arg); return token;
      };
      Object.defineProperty(method, 'call', {get() {assert.fail('mutable call helper');}});
      return method;
    }};
    const result = fn(value, payload);
    assert.ok(result === token || result === failure || (noncallable && result instanceof TypeError));
    return {trace, outcome: result === token ? 'return' : result === failure ? 'sentinel' : 'TypeError'};
  }
  for (const value of values) for (const mutate of [false, true]) for (const noncallable of [false, true])
    for (let failAt = 0; failAt <= 6; ++failAt) {
      assert.deepEqual(observe(recovered, value, mutate, failAt, noncallable),
        observe(reference, value, mutate, failAt, noncallable)); ++cases;
    }
}
console.log(`${cases} phased retained input/snapshot identity, receiver, noncallable and caught-exception scenarios passed`);
