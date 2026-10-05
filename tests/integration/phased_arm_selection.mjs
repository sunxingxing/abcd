import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const values = [undefined, null, false, true, 0, -0, 1, NaN, Infinity, '', '0', 0n,
  Symbol('value'), {}, [], function original() {},
  {[Symbol.toPrimitive]() { assert.fail('truth tests must not coerce'); }}];
let cases = 0;
for (const [option, depth, barrier, dispatch] of [
  ['--arm-selection-source', 2, false, false],
  ['--arm-selection-alias-source', 2, false, false],
  ['--arm-selection-chain-source', 4, false, false],
  ['--arm-selection-barrier-source', 2, true, false],
  ['--arm-selection-dispatch-source', 2, false, true],
]) {
  const source = execFileSync(process.argv[2], [option], {encoding: 'utf8'});
  if (!barrier && !dispatch) {
    assert.equal((source.match(/\["__proto__"\]:/g) ?? []).length, 2);
    assert.doesNotMatch(source, /\$phi|if \(|\.__proto__ = /);
  }
  if (barrier) assert.equal((source.match(/debugger;/g) ?? []).length, 2);
  const recovered = Function(`"use strict"; return ${source}`)();
  function selected(input) {
    let truth = input;
    for (let level = 1; level < depth && truth; ++level) truth = input.value;
    return truth ? 7 : null;
  }
  function reference(input) {
    if (dispatch) {
      const first = selected(input);
      try { return {['__proto__']: first, ['__proto__']: selected(input)}; }
      catch (error) { return error; }
    }
    try { return {['__proto__']: selected(input), ['__proto__']: selected(input)}; }
    catch (error) { return error; }
  }
  for (const first of values) for (const second of values) for (const failure of [0, 1, 2]) {
    const thrown = Symbol('original exception');
    const run = fn => {
      const events = []; let reads = 0;
      const input = new Proxy({}, {get(target, key, receiver) {
        assert.equal(receiver, input); assert.equal(key, 'value'); events.push(++reads);
        if (reads === failure) throw thrown;
        return reads === 1 ? first : second;
      }});
      if (dispatch && failure === 1) {
        assert.throws(() => fn(input), error => error === thrown); return events;
      }
      const result = fn(input);
      if (failure) assert.equal(result, thrown);
      else {
        assert.equal(Object.getPrototypeOf(result), Object.prototype);
        assert.deepEqual(Reflect.ownKeys(result), ['__proto__']);
        assert.deepEqual(Object.getOwnPropertyDescriptor(result, '__proto__'), {
          value: second ? 7 : null, writable: true, enumerable: true, configurable: true});
      }
      return events;
    };
    assert.deepEqual(run(recovered), run(reference)); ++cases;
  }
  for (const input of values) { assert.deepEqual(recovered(input), reference(input)); ++cases; }
  assert.notEqual(recovered({value: 1}), recovered({value: 1}));
  ++cases;
}
console.log(`${cases} nested-arm cases preserve selected getter execution, raw identity, catch scope and own properties`);
