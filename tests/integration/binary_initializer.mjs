import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {runInNewContext} from 'node:vm';

// Handwritten behavior, independent of the production opcode/token tables.
const operations = [
  (a,b) => a + b, (a,b) => a - b, (a,b) => a * b, (a,b) => a / b,
  (a,b) => a % b, (a,b) => a == b, (a,b) => a != b, (a,b) => a < b,
  (a,b) => a <= b, (a,b) => a > b, (a,b) => a >= b, (a,b) => a << b,
  (a,b) => a >>> b, (a,b) => a >> b, (a,b) => a & b, (a,b) => a | b,
  (a,b) => a ^ b, (a,b) => a ** b, (a,b) => a in b, (a,b) => a instanceof b,
  (a,b) => a !== b, (a,b) => a === b
];
let scenarios = 0;
for (const [index, reference] of operations.entries()) for (const negative of [false, true]) {
  const source = execFileSync(process.argv[2], [negative ? '--negative-source' : '--source', String(index)], {encoding: 'utf8'});
  const generated = runInNewContext(source);
  const left = negative ? -7 : 7;
  const right = index === 18 ? {} : index === 19 ? function Constructor() {} : negative ? 3 : 9;
  const result = generated(right);
  assert.deepEqual(Object.keys(result), ['field0', 'field1']);
  assert.deepEqual(Object.values(result), [reference(left, right), reference(left, right)]);
  ++scenarios;
  if (index === 19) for (const failAt of [0, 1, 2]) {
    const exercise = fn => {
      const events = [], failure = {}; let calls = 0;
      const constructor = {[Symbol.hasInstance](value) {
        events.push(value); if (++calls === failAt) throw failure; return calls === 1;
      }};
      try { events.push(Object.values(fn(constructor))); }
      catch (error) { assert.equal(error, failure); events.push('throw'); }
      return events;
    };
    assert.deepEqual(exercise(generated), exercise(constructor => ({field0: left instanceof constructor, field1: left instanceof constructor})));
    ++scenarios;
  }
}
// General parameter inputs exercise actual decoded expressions, not a hand-
// assembled AST. Rebuild all operands for each run to isolate mutation traces.
function observeArguments(fn, scenario, wrapping, mutate, failAt) {
  const events = [], failure = {}; let steps = 0;
  const event = value => { events.push(value); if (++steps === failAt) throw failure; };
  const shared = {}, symbol = Symbol('shared');
  const pairs = [[7,3], [-7,3], [0,-0], [NaN,2], [Infinity,-Infinity], ['12','2'],
    ['x',2], [null,undefined], [false,0], [1n,2n], [1n,2], [0n,0n], [-8n,2n],
    [2n,-1n], [Symbol('value'),1], [symbol,symbol], [Symbol('a'),Symbol('b')],
    [{},{}], [shared,shared], [true,'1'], [9007199254740992,9007199254740993n],
    ['9007199254740993',9007199254740993n], ['20','3'], [undefined,null]];
  let pair;
  if (scenario < pairs.length) pair = pairs[scenario];
  else switch (scenario - pairs.length) {
    case 0: pair = [{[Symbol.toPrimitive](hint) { event(['key',hint]); return 'field'; }},
      new Proxy({field: 1}, {has(target, key) { event(['has',String(key)]); return Reflect.has(target,key); }})]; break;
    case 1: {
      let calls = 0; const left = {};
      const right = {get [Symbol.hasInstance]() {
        event('get:hasInstance'); return function(value) {
          assert.equal(this,right); assert.equal(value,args[0]); event('call:hasInstance'); return ++calls === 1;
        };
      }}; pair = [left,right]; break;
    }
    case 2: {
      const target = {[Symbol.toPrimitive](hint) { assert.equal(this,proxy); event(['primitive',hint]); return 7; }};
      const proxy = new Proxy(target, {get(object,key,receiver) {
        event(['get',String(key)]); return Reflect.get(object,key,receiver);
      }}); pair = [proxy,3]; break;
    }
    case 3: pair = [{valueOf() { event('valueOf'); return {}; }, toString() { event('toString'); return '7'; }},3]; break;
    case 4: pair = [{[Symbol.toPrimitive](hint) { event(['invalid',hint]); return {}; }},3]; break;
    case 5: { const revoked = Proxy.revocable({},{}); revoked.revoke(); pair = [revoked.proxy,3]; break; }
    default: assert.fail('unknown scenario');
  }
  const state = [...pair];
  const args = pair.map((value,index) => {
    if (wrapping === 0 || (wrapping === 2 && index === 1) || (wrapping === 3 && index === 0)) return value;
    const wrapped = {[Symbol.toPrimitive](hint) {
      assert.equal(this,wrapped); event([index,hint]);
      if (mutate && index === 0) state[1] = typeof state[1] === 'bigint' ? 11n : 11;
      return state[index];
    }}; return wrapped;
  });
  try { const result = fn(...args); events.push(['return',result === undefined ? '<caught>' : Object.values(result)]); }
  catch (error) {
    if (error === failure) events.push('injected');
    else { assert.ok(error.name === 'TypeError' || error.name === 'RangeError'); events.push(error.name); }
  }
  return events;
}
let argumentScenarios = 0;
for (const phased of [false,true]) for (const [index, operation] of operations.entries()) {
  // These five retain independent arithmetic value-domain gates.
  if ([0,1,2,3,17].includes(index)) continue;
  const source = execFileSync(process.argv[2], [phased ? '--phased-argument-source' : '--argument-source',String(index)], {encoding:'utf8'});
  const generated = runInNewContext(source);
  const reference = (left,right) => {
    try { return {field0: operation(left,right), field1: operation(left,right)}; }
    catch (error) { if (phased) return undefined; throw error; }
  };
  for (let scenario = 0; scenario < 30; ++scenario) for (let wrapping = 0; wrapping < 4; ++wrapping)
    for (const mutate of [false,true]) for (let failAt = 0; failAt <= 4; ++failAt) {
      assert.deepEqual(observeArguments(generated,scenario,wrapping,mutate,failAt),
        observeArguments(reference,scenario,wrapping,mutate,failAt),
        `phased ${phased}, operator ${index}, scenario ${scenario}, wrapping ${wrapping}, mutation ${mutate}, failure ${failAt}`);
      ++argumentScenarios;
    }
}
console.log(`Decoded parameter operators: ${argumentScenarios} coercion/identity/exception scenarios passed`);

const exponentSource = execFileSync(process.argv[3], ['--source'], {encoding: 'utf8'});
const samples = runInNewContext(exponentSource.replace('export default', ''));
const view = new DataView(new ArrayBuffer(8));
const number = hex => { view.setBigUint64(0, BigInt(`0x${hex}`)); return view.getFloat64(0); };
for (const sample of samples) {
  let expected = number(sample.left);
  if (sample.update === 1) ++expected;
  else if (sample.update === -1) --expected;
  else expected = expected ** number(sample.right);
  assert.ok(Object.is(sample.fn(), expected), `${sample.left}, exponent/update ${sample.right ?? sample.update}`);
  ++scenarios;
}
for (const option of ['--source', '--increment-source', '--decrement-source']) {
  const nestedSource = execFileSync(process.argv[4], [option], {encoding: 'utf8'});
  for (const sample of runInNewContext(nestedSource.replace('export default', ''))) {
    let expected = sample.seed;
    for (let i = 0; i < 2; ++i) expected = expected ** sample.step;
    assert.ok(Object.is(sample.fn(), expected)); ++scenarios;
  }
}
console.log(`Decoded binary/comparison expressions: ${scenarios} native scenarios passed`);
