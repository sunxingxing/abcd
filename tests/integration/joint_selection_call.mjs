import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const tool = process.argv[2];
const token = {}, failure = {}, symbol = Symbol('argument');
const hostile = {[Symbol.toPrimitive]() { assert.fail('condition coerced'); }};
const flags = [undefined, null, false, 0, -0, NaN, '', true, 1, 1n, symbol, hostile];
const options = [undefined, null, false, 0, -0, NaN, 'argument', symbol, token];
let cases = 0;
for (const form of ['plain', 'prefix', 'wrapped', 'dual', 'dual-prefix', 'dual-wrapped', 'dual-copies', 'dual-copies-prefix', 'dual-copies-wrapped', 'dual-reference-copies', 'dual-reference-copies-prefix', 'dual-reference-copies-wrapped']) for (const count of [1, 2, 3, 200, 201]) {
  const prefix = form.endsWith('prefix'), wrapped = form.endsWith('wrapped'), dual = form.startsWith('dual');
  const source = execFileSync(tool, [form === 'plain' ? '--source' : `--${form}-source`, String(count)], {encoding: 'utf8'});
  assert.ok(source.includes('value.method(') && source.includes(' ? '));
  assert.ok(!source.includes('.call(') && !source.includes('INCOMPLETE'));
  const recovered = Function(`"use strict"; return ${source}`)();
  // An independent language expression fixes reference acquisition and every
  // argument evaluation before the actual call. It never enters engine inputs.
  const selected = `value.probe ? ${dual ? 'value.alternative' : '7'} : value.option`;
  const argument = prefix ? `(value.other, ${selected})` : `(${selected})`;
  const call = `value.method(${Array(count).fill(argument).join(', ')})`;
  const reference = Function(`"use strict"; return function(value) {
    try { return ${wrapped ? `value.other(${call})` : call}; }
    catch (error) { return error; }
  }`)();
  function run(fn, flag, option, replace, failAt, noncallable) {
    const trace = []; let step = 0, version = 0, predicate = 0;
    const expected = [];
    const event = (kind, ...args) => {
      trace.push([kind, ...args]);
      if (++step === failAt) throw failure;
    };
    const value = new Proxy({}, {get(_target, key, receiver) {
      assert.equal(receiver, value);
      if (key === 'method') {
        event('get-method', version);
        const captured = version;
        if (noncallable) return null;
        const method = function(...args) {
          assert.equal(this, value);
          assert.equal(args.length, count);
          assert.equal(expected.length, count);
          for (let i = 0; i < count; ++i) assert.ok(Object.is(args[i], expected[i]));
          event('call', captured, ...args);
          return token;
        };
        Object.defineProperty(method, 'call', {get() { assert.fail('mutable .call helper'); }});
        return method;
      }
      if (key === 'other' && wrapped) {
        event('get-outer', version);
        const captured = version;
        const method = function(argument) {
          assert.equal(this, value); assert.equal(argument, token);
          event('outer-call', captured); return token;
        };
        Object.defineProperty(method, 'call', {get() { assert.fail('outer .call helper'); }});
        return method;
      }
      if (key === 'other') {
        assert.ok(prefix); event('prefix', version);
        if (replace) ++version;
        return token;
      }
      if (key === 'probe') {
        event('predicate', predicate, version);
        // Interleaved truth values expose accidental sharing of one argument's
        // decision or phi with another, including its retained getter value.
        const selected = predicate++ % 2 ? !flag : flag;
        if (replace) ++version;
        if (selected && !dual) expected.push(7);
        return selected;
      }
      if (key === 'alternative') {
        assert.ok(dual); event('alternative', version);
        if (replace) ++version;
        expected.push(token); return token;
      }
      assert.equal(key, 'option'); event('option', version);
      if (replace) ++version;
      expected.push(option);
      return option;
    }});
    const result = fn(value);
    assert.ok(result === token || result === failure || (noncallable && result instanceof TypeError));
    return {trace, outcome: result === token ? 'return' : result === failure ? 'sentinel' : 'TypeError'};
  }
  const selectedFlags = count > 3 ? [false, true, symbol, hostile] : flags;
  const selectedOptions = count > 3 ? [token, symbol] : options;
  const failures = count > 3 ? [0, 1, 2, count, count + 1, 2 * count, 2 * count + 1, 3 * count, 3 * count + 2] :
    Array.from({length: 4 * count + 3}, (_, i) => i);
  for (const flag of selectedFlags) for (const option of selectedOptions) for (const replace of [false, true])
    for (const failAt of failures) for (const noncallable of [false, true]) {
      assert.deepEqual(run(recovered, flag, option, replace, failAt, noncallable),
        run(reference, flag, option, replace, failAt, noncallable));
      ++cases;
    }
  if (count === 2) {
    const mutants = [source.replace('value.method(', '(0, value.method)('),
      source.replace('value.method(', 'value.method(7, '),
      source.replace(`? value.option : ${dual ? 'value.alternative' : '7'}`, `? ${dual ? 'value.alternative' : '7'} : value.option`)];
    for (const mutated of mutants) {
      assert.notEqual(mutated, source);
      const mutant = Function(`"use strict"; return ${mutated}`)();
      assert.throws(() => assert.deepEqual(run(mutant, false, token, true, 0, false),
        run(reference, false, token, true, 0, false)));
    }
  }
}
console.log(`${cases} joint conditional arguments preserve reference, receiver, order and exceptions`);
