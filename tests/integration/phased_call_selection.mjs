import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const tool = process.argv[2];
const token = {}, failure = {}, symbol = Symbol('argument');
const hostile = {[Symbol.toPrimitive]() { assert.fail('condition coerced'); }};
const flags = [undefined, null, false, 0, -0, NaN, '', true, 1, 1n, symbol, hostile];
const options = [undefined, null, false, 0, -0, NaN, 'argument', symbol, token];
let cases = 0;
const variants = [{computed: false, prefix: 'none'}, {computed: true, prefix: 'none'},
  {computed: true, prefix: 'getter'}, {computed: true, prefix: 'call'},
  {computed: true, prefix: 'nullish'}, {computed: true, prefix: 'wrapped'}];
for (const {computed, prefix} of variants) for (const count of [1, 2]) {
  const nullable = prefix === 'nullish' || prefix === 'wrapped';
  const source = execFileSync(tool, [prefix === 'nullish' ? '--phased-nullish-source' : prefix === 'wrapped' ? '--phased-wrapped-nullish-source' : prefix === 'getter' ? '--phased-prefix-getter-source' : prefix === 'call' ? '--phased-prefix-call-source' :
    computed ? '--phased-computed-call-selection-source' : '--phased-call-selection-source', ...(count === 2 ? ['--twice'] : [])], {encoding: 'utf8'});
  assert.ok(source.includes('value.method(') && source.includes(' ? '));
  assert.ok(!source.includes('.call(') && !source.includes('INCOMPLETE'));
  const recovered = Function(`"use strict"; return ${source}`)();
  const reference = (value, flag, wrap) => {
    try {
      let result;
      for (let i = 0; i < count; ++i) {
        if (nullable) {
          let parsed;
          if (prefix === 'wrapped') result = wrap(value.method((parsed = value.probe(flag), parsed?.option)));
          else result = value.method((parsed = value.probe(flag), parsed?.option));
        } else if (prefix === 'getter') result = value.method(value.probe ? 7 : value.option);
        else if (prefix === 'call') {
          let parsed;
          result = value.method((parsed = value.probe(flag), parsed.other ? 7 : parsed.option));
        } else result = value.method(flag ? 7 : computed ? value.option : 448);
      }
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
          assert.ok(Object.is(argument, nullable ? (flag == null ? undefined : option) : flag ? 7 : computed ? option : 448));
          event('call', capturedVersion, argument);
          return token;
        };
        Object.defineProperty(method, 'call', {get() { assert.fail('mutable .call helper'); }});
        return method;
      }
      if (key === 'probe') {
        assert.notEqual(prefix, 'none'); event('get-probe');
        if (replace) ++version;
        if (prefix === 'getter') return flag;
        const probe = function(input) {
          assert.equal(this, value); assert.ok(Object.is(input, flag)); event('probe-call');
          if (replace) ++version;
          if (nullable && (flag == null || (typeof flag !== 'object' && typeof flag !== 'function'))) return flag;
          const parsed = {
            get other() {
              assert.equal(this, parsed); event('get-other'); if (replace) ++version; return flag;
            },
            get option() {
              assert.equal(this, parsed); event('get-parsed-option'); if (replace) ++version; return option;
            },
          };
          return parsed;
        };
        Object.defineProperty(probe, 'call', {get() { assert.fail('prefix .call helper'); }});
        return probe;
      }
      assert.equal(key, 'option'); event('get-option');
      if (replace) ++version;
      return option;
    }});
    // The original bytecode handler returns the exception; an escaping throw
    // is a failure, not an interchangeable outcome in this harness.
    const prototype = nullable && flag != null ?
      ({boolean: Boolean.prototype, number: Number.prototype, string: String.prototype,
        bigint: BigInt.prototype, symbol: Symbol.prototype})[typeof flag] : undefined;
    const previous = prototype && Object.getOwnPropertyDescriptor(prototype, 'option');
    if (prototype) Object.defineProperty(prototype, 'option', {configurable: true, get() {
      assert.ok(Object.is(this, flag)); event('primitive-option'); if (replace) ++version; return option;
    }});
    const wrap = function(argument) {
      assert.equal(this, undefined); assert.equal(argument, token); event('wrap'); return token;
    };
    Object.defineProperty(wrap, 'call', {get() { assert.fail('wrapper .call helper'); }});
    try {
      const result = fn(value, flag, wrap);
      assert.ok(result === token || result === failure || (noncallable && result instanceof TypeError));
      return {trace, outcome: result === token ? 'return' : result === failure ? 'sentinel' : 'TypeError'};
    } finally {
      if (prototype) {
        if (previous) Object.defineProperty(prototype, 'option', previous);
        else delete prototype.option;
      }
    }
  }
  for (const flag of flags) for (const option of options) for (const replace of [false, true])
    for (const failAt of Array.from({length: 13}, (_, i) => i)) for (const noncallable of [false, true]) {
      assert.deepEqual(run(recovered, flag, option, replace, failAt, noncallable),
        run(reference, flag, option, replace, failAt, noncallable));
      ++cases;
    }
}
console.log(`${cases} phased conditional call receiver/order/exception scenarios passed`);
