import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const source = execFileSync(process.argv[2], ['--predicate-read-source'], {encoding: 'utf8'});
assert.equal((source.match(/\["__proto__"\]:/g) ?? []).length, 2);
assert.doesNotMatch(source, /\.__proto__ = /);
const recovered = Function(`"use strict"; return ${source}`)();
function reference(input) {
  try {
    const object = {['__proto__']: null};
    for (let field = 0; field < 2; ++field) {
      const current = input.value;
      Object.defineProperty(object, '__proto__', {
        value: current === false || current === 0 ? null : 7,
        writable: true, enumerable: true, configurable: true,
      });
    }
    return object;
  } catch (error) { return error; }
}
const values = [undefined, null, false, true, 0, -0, 1, NaN, Infinity,
  '', '0', 0n, Symbol('value'), {}, [], function original() {}];
let cases = 0;
for (const first of values) for (const second of values) {
  for (const failure of [0, 1, 2]) {
    const thrown = Symbol('thrown');
    function run(fn) {
      const trace = []; let ordinal = 0;
      const input = new Proxy({}, {
        get(target, key, receiver) {
          assert.equal(key, 'value'); assert.equal(receiver, input);
          const current = ++ordinal; trace.push(current);
          if (current === failure) throw thrown;
          return current === 1 ? first : second;
        },
      });
      const result = fn(input);
      if (failure) assert.equal(result, thrown);
      else {
        assert.equal(Object.getPrototypeOf(result), Object.prototype);
        assert.deepEqual(Reflect.ownKeys(result), ['__proto__']);
        assert.deepEqual(Object.getOwnPropertyDescriptor(result, '__proto__'), {
          value: second === false || second === 0 ? null : 7,
          writable: true, enumerable: true, configurable: true,
        });
      }
      return trace;
    }
    assert.deepEqual(run(recovered), run(reference)); ++cases;
  }
}
const conversion = {[Symbol.toPrimitive]() { assert.fail('raw branch must not coerce'); }};
const input = {get value() { return conversion; }};
assert.notEqual(recovered(input), recovered(input));
assert.deepEqual(Object.getOwnPropertyDescriptors(recovered(input)), Object.getOwnPropertyDescriptors(reference(input)));
console.log(`${cases + 1} predicate prefix cases preserve repeated reads, raw predicate semantics, exceptions and own descriptors`);
