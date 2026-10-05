import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

// Independent language AST oracle. The fixture checks grammar/origin spans;
// actual decoded conditional-call and paired-record tests cover engine input.
const operations = [(a,b) => a == b, (a,b) => a != b, (a,b) => a === b, (a,b) => a !== b,
  (a,b) => a < b, (a,b) => a <= b, (a,b) => a > b, (a,b) => a >= b,
  (a,b) => a in b, (a,b) => a instanceof b];
function reference(index, depth) {
  return (a,b,c) => {
    let value = operations[index % 10](a.value,b.value);
    for (let i = 0; i < depth; ++i) value = !value;
    switch (Math.floor(index / 10)) {
      case 0: return value;
      case 1: return value ? b.value : c.value;
      case 2: return value ** c.value;
      case 3: return typeof value;
      case 4: return ~value;
      case 5: return value === c.value;
      default: assert.fail('unknown context');
    }
  };
}
function observe(fn, left, right, mutate, failAt) {
  const trace = [], failure = Symbol('failure'); let steps = 0;
  const event = (...parts) => {trace.push(parts); if (++steps === failAt) throw failure;};
  const symbol = Symbol('value'), shared = {};
  const converting = {[Symbol.toPrimitive](hint) {event('primitive',hint); return 3;}};
  const invalid = {[Symbol.toPrimitive](hint) {event('invalid',hint); return {};}};
  const values = [undefined,null,false,true,0,-0,NaN,Infinity,-Infinity,'','2','text',1n,
    symbol,shared,converting,invalid];
  const state = [values[left],values[right],values[right]];
  const args = state.map((_, index) => {
    let receiver;
    const target = Object.defineProperty({},'value',{get() {
      assert.equal(this,receiver); event('getter',index);
      if (mutate && index === 0) state[1] = state[2] = null;
      return state[index];
    }});
    receiver = new Proxy(target,{get(target,key,owner) {
      assert.equal(key,'value'); event('get',index); return Reflect.get(target,key,owner);
    }});
    return receiver;
  });
  try {
    const value = fn(...args);
    if (typeof value === 'symbol' || (typeof value === 'object' && value !== null)) {
      const identity = values.indexOf(value); assert.ok(identity >= 13);
      return {trace,inputIdentity:identity};
    }
    return {trace,value};
  } catch (error) {
    if (error === failure) return {trace,threw:'injected'};
    assert.equal(error.name,'TypeError'); return {trace,threw:'TypeError'};
  }
}
let comparisons = 0;
let mutations = 0;
for (let index = 0; index < 60; ++index) for (const depth of [1,2,200,201,402,804]) {
  const source = execFileSync(process.argv[2],['--predicate-polarity',String(index),String(depth)],{encoding:'utf8'});
  assert.doesNotMatch(source,/INCOMPLETE/);
  if (index < 4) assert.doesNotMatch(source,/return !/);
  if (Math.floor(index / 10) === 1) assert.doesNotMatch(source,/return \(!/);
  const actual = vm.runInNewContext(source), expected = reference(index,depth);
  if (index === 14 && depth === 1) {
    const wrongBranch = source.replace('? c.value : b.value','? b.value : c.value');
    const replayedRead = source.replace('a.value <','(a.value, a.value) <');
    for (const changed of [wrongBranch,replayedRead]) {
      assert.notEqual(changed,source);
      assert.notDeepEqual(observe(vm.runInNewContext(changed),4,10,false,0),
        observe(expected,4,10,false,0));
      ++mutations;
    }
  }
  for (let left = 0; left < 17; ++left) for (let right = 0; right < (depth === 1 ? 17 : 1); ++right)
    for (const mutate of [false,true]) for (let failAt = 0; failAt < 12; ++failAt) {
      assert.deepEqual(observe(actual,left,right,mutate,failAt),observe(expected,left,right,mutate,failAt),
        `operator ${index % 10} context ${Math.floor(index / 10)} depth ${depth}`);
      ++comparisons;
    }
}
// A relational complement would be wrong for NaN even without user effects.
assert.notEqual(!(NaN < 1),NaN >= 1);
assert.equal(mutations,2);
console.log(`${comparisons} predicate polarity value/branch/grammar/getter/coercion/throw comparisons passed; two mutations detected`);
