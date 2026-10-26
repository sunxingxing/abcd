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
  const nanView = new DataView(new ArrayBuffer(8));
  nanView.setBigUint64(0, 0xfff7000000000005n);
  const payloadNaN = nanView.getFloat64(0);
  const pairs = [[7,3], [-7,3], [0,-0], [NaN,2], [Infinity,-Infinity], ['12','2'],
    ['x',2], [null,undefined], [false,0], [1n,2n], [1n,2], [0n,0n], [-8n,2n],
    [2n,-1n], [Symbol('value'),1], [symbol,symbol], [Symbol('a'),Symbol('b')],
    [{},{}], [shared,shared], [true,'1'], [9007199254740992,9007199254740993n],
    ['9007199254740993',9007199254740993n], ['20','3'], [undefined,null], [payloadNaN,3]];
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
  for (let scenario = 0; scenario < 31; ++scenario) for (let wrapping = 0; wrapping < 4; ++wrapping)
    for (const mutate of [false,true]) for (let failAt = 0; failAt <= 4; ++failAt) {
      assert.deepEqual(observeArguments(generated,scenario,wrapping,mutate,failAt),
        observeArguments(reference,scenario,wrapping,mutate,failAt),
        `phased ${phased}, operator ${index}, scenario ${scenario}, wrapping ${wrapping}, mutation ${mutate}, failure ${failAt}`);
      ++argumentScenarios;
    }
}
console.log(`Decoded parameter operators: ${argumentScenarios} coercion/identity/exception scenarios passed`);

// Both shifts produce finite Numbers, including zero. Keep their conversion
// effects ordered before multiplication, then preserve the product's grouping.
let productScenarios = 0;
for (const phased of [false,true]) {
  const source = execFileSync(process.argv[2], [phased ? '--phased-integer-product-source' : '--integer-product-source'], {encoding:'utf8'});
  const generated = runInNewContext(source);
  const reference = (left,right) => {
    try { return {field0: ((left << 0) * (right << 0)) ** 2,
                  field1: ((left << 0) * (right << 0)) ** 2}; }
    catch (error) { if (phased) return undefined; throw error; }
  };
  for (let scenario = 0; scenario < 31; ++scenario) for (let wrapping = 0; wrapping < 4; ++wrapping)
    for (const mutate of [false,true]) for (let failAt = 0; failAt <= 4; ++failAt) {
      assert.deepEqual(observeArguments(generated,scenario,wrapping,mutate,failAt),
        observeArguments(reference,scenario,wrapping,mutate,failAt));
      ++productScenarios;
    }
}
console.log(`Finite product composition: ${productScenarios} scenarios passed`);

let bitwiseCombinations = 0;
for (const index of [11,12,13,14,15,16]) for (const mode of ['left','right','both','converted']) {
  if (mode === 'both' && index !== 12) continue;
  for (const phased of [false,true]) {
    const source = execFileSync(process.argv[2], [phased ? '--phased-square-source' : '--square-source', String(index), mode], {encoding:'utf8'});
    const generated = runInNewContext(source);
    const reference = (left,right) => {
      const calculate = () => operations[index](mode === 'left' ? 1 : mode === 'converted' ? +left : left, mode === 'right' ? 1 : mode === 'converted' ? +right : right) ** 2;
      try { return {field0:calculate(), field1:calculate()}; }
      catch (error) { if (phased) return undefined; throw error; }
    };
    for (let scenario = 0; scenario < 31; ++scenario) for (let wrapping = 0; wrapping < 4; ++wrapping)
      for (const mutate of [false,true]) for (let failAt = 0; failAt <= 4; ++failAt) {
        assert.deepEqual(observeArguments(generated,scenario,wrapping,mutate,failAt),
          observeArguments(reference,scenario,wrapping,mutate,failAt),
          `bitwise ${index}, ${mode}, phased ${phased}, scenario ${scenario}`);
        ++bitwiseCombinations;
      }
  }
}
console.log(`Decoded bitwise compositions: ${bitwiseCombinations} coercion/exception scenarios passed`);
const exponentSource = execFileSync(process.argv[3], ['--source'], {encoding: 'utf8'});
const samples = runInNewContext(exponentSource.replace('export default', ''));
const view = new DataView(new ArrayBuffer(8));
const number = hex => { view.setBigUint64(0, BigInt(`0x${hex}`)); return view.getFloat64(0); };
for (const sample of samples) {
  let expected = number(sample.left);
  if (sample.escaped) { /* Original returns the shared ordinary NaN. */ }
  else if (sample.binary !== undefined) expected = operations[sample.binary](expected, number(sample.right));
  else if (sample.bitnot) expected = ~expected;
  else if (sample.update === 1) ++expected;
  else if (sample.update === -1) --expected;
  else expected = expected ** number(sample.right);
  assert.ok(Object.is(sample.fn(), expected), `${sample.left}, exponent/update ${sample.right ?? sample.update}`);
  ++scenarios;
}
for (const option of ['--source', '--increment-source', '--decrement-source', '--bitnot-source']) {
  const nestedSource = execFileSync(process.argv[4], [option], {encoding: 'utf8'});
  for (const sample of runInNewContext(nestedSource.replace('export default', ''))) {
    let expected = sample.seed;
    for (let i = 0; i < 2; ++i) expected = expected ** sample.step;
    assert.ok(Object.is(sample.fn(), expected)); ++scenarios;
  }
}
console.log(`Decoded binary/comparison expressions: ${scenarios} native scenarios passed`);

// Numeric facts come from decoded literals/comparisons, not argument types.
// Rebuild comparison inputs on each run so getter/coercion mutations remain
// observable across both fields; strict equality must not invoke those hooks.
let primitiveScenarios = 0;
for (const phased of [false,true]) for (let mode=0;mode<19;++mode)
  for (const index of mode===10 ? [0] : [0,1,2,3,17]) {
    const source=execFileSync(process.argv[2],['--primitive-source',String(index),String(mode),phased?'phased':'normal'],{encoding:'utf8'});
    const generated=runInNewContext(source);
    const reference=(left,right) => {
      const seed=() => {
        switch(mode) {
          case 0:return true; case 1:return null; case 2:return left<right;
          case 3:return left===right; case 4:return +true; case 5:return -true;
          case 6:return -null; case 7:return +true; case 8:return false;
          case 9:return left===false || left===0 ? null : true;
          case 10:return typeof left;
          case 11:return true;case 12:return null;case 13:return false;
          case 14:{let value=true;return ++value;}
          case 15:{let value=true;return --value;}
          case 16:{let value=true;return ++value;}
          case 17:{let value=true;return --value;}
          case 18:return ~true;
          default:assert.fail('unknown fixture seed');
        }
      };
      const calculate=() => mode>=11 && mode<=13 ? operations[index](2,seed()) : operations[index](seed(),2);
      try {return {field0:calculate(),field1:calculate()};}
      catch(error) {if(phased)return undefined;throw error;}
    };
    for(let scenario=0;scenario<31;++scenario) for(const wrapping of [0,1])
      for(const mutate of [false,true]) for(const failAt of [0,1,2,3,4]) {
        assert.deepEqual(observeArguments(generated,scenario,wrapping,mutate,failAt),
          observeArguments(reference,scenario,wrapping,mutate,failAt),
          `primitive ${mode}, operator ${index}, phased ${phased}, scenario ${scenario}, wrapping ${wrapping}, mutate ${mutate}, failAt ${failAt}`);
        ++primitiveScenarios;
      }
    assert.ok(!source.includes('UNRECOVERED'));
  }
console.log(`Decoded primitive arithmetic: ${primitiveScenarios} scenarios passed`);
