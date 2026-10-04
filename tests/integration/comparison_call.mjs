import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const source = execFileSync(process.argv[2], ['--source'], {encoding: 'utf8', timeout: 120_000, maxBuffer: 16 * 1024 * 1024});
assert.doesNotMatch(source, /INCOMPLETE|switch\s*\(|Reflect\.|\.call\s*\(/);
const {default: functions} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
assert.equal(functions.length, 150);
const comparisons = [(a,b) => a == b, (a,b) => a != b, (a,b) => a < b, (a,b) => a <= b,
  (a,b) => a > b, (a,b) => a >= b, (a,b) => a in b, (a,b) => a instanceof b,
  (a,b) => a === b, (a,b) => a !== b];
const alternate = Symbol('alternate'), result = Symbol('result'), failure = Symbol('failure');
function run(fn, scenario, failAt, mutate) {
  const events = [];
  const event = (...parts) => {events.push(parts); if (events.length === failAt) throw failure;};
  let version = 0;
  const methods = [0, 1].map(id => {
    const method = function(value) {assert.equal(this, target); event('call', id, value); return result;};
    Object.defineProperty(method, 'call', {get() {assert.fail('must preserve the native member Reference');}});
    return method;
  });
  const target = {get method() {assert.equal(this, target); event('get:method'); return methods[version];}};
  const shared = {}, symbol = Symbol('pair');
  const pairs = [[1,2], [2,1], [1,1], [0,-0], [NaN,1], [Infinity,-Infinity], ['2','10'],
    ['2',2], [null,undefined], [true,1], [false,0], [1n,2n], [2n,2], [symbol,symbol],
    [symbol,1], [{},{}], [shared,shared], ['',0], [undefined,0], ['field',{field:1}],
    ['missing',{field:1}], [symbol,{[symbol]:1}], [{},function Constructor() {}], [1,function Constructor() {}]];
  let pair;
  if (scenario < pairs.length) pair = pairs[scenario];
  else switch (scenario - pairs.length) {
    case 0: pair = [{[Symbol.toPrimitive](hint) {event('primitive:left',hint); return 2;}},
      {[Symbol.toPrimitive](hint) {event('primitive:right',hint); return 3;}}]; break;
    case 1: pair = [{valueOf() {event('valueOf'); return {};}, toString() {event('toString'); return '2';}},3]; break;
    case 2: pair = [{[Symbol.toPrimitive](hint) {event('invalid',hint); return {};}},3]; break;
    case 3: pair = [{[Symbol.toPrimitive](hint) {event('key',hint); return 'field';}},
      new Proxy({field:1}, {has(object,key) {event('has',key); return Reflect.has(object,key);}})]; break;
    case 4: {
      const right = {get [Symbol.hasInstance]() {event('get:hasInstance'); return function(value) {
        assert.equal(this,right); assert.equal(value,pair[0]); event('call:hasInstance'); return true;
      };}}; pair = [{}, right]; break;
    }
    case 5: {
      const revoked = Proxy.revocable({},{}); revoked.revoke(); pair = ['field',revoked.proxy]; break;
    }
    case 6: pair = [9007199254740992,9007199254740993n]; break;
    case 7: pair = ['9007199254740993',9007199254740993n]; break;
    default: assert.fail('unknown scenario');
  }
  const subject = {
    get left() {assert.equal(this,subject); event('get:left'); if (mutate) version ^= 1; return pair[0];},
    get right() {assert.equal(this,subject); event('get:right'); return pair[1];},
    get alternate() {assert.equal(this,subject); event('get:alternate'); return alternate;}
  };
  try {return {events, returned:true, value:fn(target,subject)};}
  catch (error) {
    if (error === failure) return {events, returned:false, value:failure};
    assert.equal(error.name,'TypeError'); return {events, returned:false, value:'TypeError'};
  }
}
let index = 0, scenarios = 0;
for (let profile = 0; profile < 3; ++profile) for (const comparison of comparisons)
  for (let form = 0; form < 5; ++form) {
    const fn = functions[index++];
    const reference = (target, subject) => {
      let value;
      for (let i = 0; i < 2; ++i) value = target.method((form === 3 ? !comparison(subject.left,subject.right) :
        comparison(subject.left,subject.right)) ? 7 : subject.alternate);
      return value;
    };
    for (let scenario = 0; scenario < 32; ++scenario) for (const mutate of [false,true])
      for (let failAt = 0; failAt < 14; ++failAt) {
        assert.deepEqual(run(fn,scenario,failAt,mutate),run(reference,scenario,failAt,mutate),
          `profile ${profile} form ${form} scenario ${scenario} failAt ${failAt}`);
        ++scenarios;
      }
  }
console.log(JSON.stringify({functions:functions.length, scenarios,
  evidence:'decoded comparison predicates in native member-call conditional arguments; callee mutation, receiver, coercion/getter order/count, thrown identity; copies, block splitting and logical inversion'}));
