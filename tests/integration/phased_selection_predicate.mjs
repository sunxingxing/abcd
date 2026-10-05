import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

let cases = 0;
for (const [option, unary, dispatch, depth] of [
  ['--selection-predicate-source', false, false, 2],
  ['--selection-predicate-unary-source', true, false, 2],
  ['--selection-predicate-dispatch-source', false, true, 2],
  ['--selection-predicate-chain-source', false, false, 4],
]) {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  if (!dispatch) {
    assert.equal((source.match(/\["__proto__"\]:/g) ?? []).length, 2);
    assert.doesNotMatch(source, /\.__proto__ = /);
    assert.doesNotMatch(source, /if \(/);
  }
  const recovered = Function(`"use strict"; return ${source}`)();
  function selected(input) {
    let value = input ? input.value : null;
    if (unary) value = typeof value;
    for (let level = 1; level < depth; ++level) value = value === null ? 7 : null;
    return value;
  }
  function reference(input) {
    const object = {['__proto__']: null};
    if (dispatch) {
      const first = input ? input.value : null;
      try {
        Object.defineProperty(object, '__proto__', {value: first === null ? 7 : null,
          writable: true, enumerable: true, configurable: true});
        Object.defineProperty(object, '__proto__', {value: selected(input),
          writable: true, enumerable: true, configurable: true});
        return object;
      } catch (error) { return error; }
    }
    try {
      for (let field = 0; field < 2; ++field)
        Object.defineProperty(object, '__proto__', {value: selected(input),
          writable: true, enumerable: true, configurable: true});
      return object;
    } catch (error) { return error; }
  }
  const values = [undefined, null, false, true, 0, -0, 1, NaN, Infinity,
    '', '0', 0n, Symbol('value'), {}, [], function original() {}];
  for (const first of values) for (const second of values) {
    for (const failure of [0, 1, 2]) {
      const thrown = Symbol('thrown');
      function run(fn) {
        const trace = []; let ordinal = 0;
        const input = new Proxy({}, {
          get(target, key, receiver) {
            assert.equal(key, 'value'); assert.equal(receiver, input);
            trace.push(++ordinal);
            if (ordinal === failure) throw thrown;
            return ordinal === 1 ? first : second;
          },
        });
        if (dispatch && failure === 1) assert.throws(() => fn(input), error => error === thrown);
        else {
          const result = fn(input);
          if (failure) assert.equal(result, thrown);
          else {
            assert.equal(Object.getPrototypeOf(result), Object.prototype);
            assert.deepEqual(Reflect.ownKeys(result), ['__proto__']);
            assert.deepEqual(Object.getOwnPropertyDescriptor(result, '__proto__'), {
              value: !unary && second === null ? 7 : null,
              writable: true, enumerable: true, configurable: true,
            });
          }
        }
        return trace;
      }
      assert.deepEqual(run(recovered), run(reference)); ++cases;
    }
  }
  for (const input of values) {
    // Primitive truthiness and property reads use the source language rules.
    assert.deepEqual(recovered(input), reference(input)); ++cases;
  }
  const conversion = {[Symbol.toPrimitive]() { assert.fail('pure comparison/typeOf must not coerce'); }};
  const input = {get value() { return conversion; }};
  assert.notEqual(recovered(input), recovered(input));
  assert.deepEqual(recovered(input), reference(input)); ++cases;
}
console.log(`${cases} nested selection cases preserve getter count, receiver, catch scope and native object syntax`);
