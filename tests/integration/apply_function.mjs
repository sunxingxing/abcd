import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const poison = {poison: true};
const records = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8'}).trim().split('\n').map(JSON.parse);
assert.equal(records.length, 8);
const payloads = [undefined, null, false, true, 0, -0, NaN, Infinity, '', 'text', 0n, 1n, Symbol('identity'), {}, [], () => {}];
const construct = Reflect.construct;
let cases = 0;
for (const record of records) {
  const context = vm.createContext({poison});
  vm.runInContext(`
    'use strict';
    for (const key of ['0', '1']) Object.defineProperty(Array.prototype, key, {
      get() {throw poison;}, set() {throw poison;}, configurable: true
    });
    Array.prototype[Symbol.iterator] = function() {throw poison;};
    Reflect.construct = function() {throw poison;};
    Function.prototype.apply = function() {throw poison;};
    Array = function() {throw poison;};
  `, context);
  const recovered = vm.runInContext(`'use strict';\n${record.source}`, context);
  // Independent index-copy oracle: no iterator and no recovered syntax.
  function reference(input) {
    const Known = record.arrow ? (first, second) => first : function Known(first, second) {return first;};
    const run = () => {
      const args = [];
      if (!record.empty) for (let index = 0; index < 2; ++index)
        Object.defineProperty(args, String(index), {value: input.value, writable: true, enumerable: true, configurable: true});
      const copied = [];
      for (let index = 0, length = args.length; index < length; ++index) copied[index] = args[index];
      return construct(Known, copied, Known);
    };
    if (!record.caught) return run();
    try {return run();} catch (error) {return error;}
  }
  function scenario(fn, first, second, fail) {
    const trace = []; let reads = 0;
    const input = {get value() {
      trace.push(reads);
      if (++reads === fail) throw first;
      return reads === 1 ? first : second;
    }};
    let result, threw = false;
    try {result = fn(input);} catch (error) {result = error; threw = true;}
    const getterFailed = !record.empty && fail !== 0;
    assert.equal(threw, !record.caught && (getterFailed || record.arrow));
    if (getterFailed) assert.ok(Object.is(result, first));
    else if (record.arrow) assert.equal(result.name, 'TypeError');
    else if (!record.empty && first !== null && (typeof first === 'object' || typeof first === 'function'))
      assert.equal(result, first);
    else {
      const constructor = Object.getPrototypeOf(result).constructor;
      assert.equal(constructor.name, 'Known'); assert.equal(constructor.length, 2);
      assert.deepEqual(Reflect.ownKeys(result), []);
    }
    return trace;
  }
  for (const first of payloads) for (const second of payloads) for (const fail of [0, 1, 2]) {
    assert.deepEqual(scenario(recovered, first, second, fail), scenario(reference, first, second, fail));
    ++cases;
  }
  if (!record.arrow) {
    const first = recovered({value: 0}), second = recovered({value: 0});
    assert.notEqual(first, second);
    assert.notEqual(Object.getPrototypeOf(first), Object.getPrototypeOf(second));
    ++cases;
  }
}
console.log(`${cases} direct-function apply construction, identity, getter order, abrupt completion and arrow refusal scenarios passed`);
