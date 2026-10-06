import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';

const rows = execFileSync(process.argv[2],['--boolean-phi-sources'],{encoding:'utf8'})
  .trim().split('\n').map(JSON.parse);
const values = [undefined,null,false,true,0,-0,1,-1,NaN,Infinity,'', 'value',1n,Symbol('value'),{},[]];
let cases = 0;
const verify = ({source,mixed,converted,compound},count = true) => {
  const actual = vm.runInNewContext(`(${source})`);
  // VM JEQZ checks exactly false or Number zero. It must not coerce a getter
  // result, and a mixed null arm must still produce a Boolean at ISTRUE.
  const reference = value => {
    if (compound) return !(value.probe === null || value.probe === undefined);
    const raw = value.probe;
    const probe = converted ? !!raw : raw;
    const selected = probe === false || probe === 0 ? mixed ? null : false : true;
    return !!selected;
  };
  for (const value of values) for (const fail of [false,true]) {
    const observe = fn => {
      let reads = 0, coerces = 0;
      const original = Symbol('getter error');
      const argument = {get probe() {++reads; if (fail) throw original; return value;}};
      if (value && typeof value === 'object') Object.defineProperty(value,Symbol.toPrimitive,{configurable:true,
        value() {++coerces; throw Error('unexpected coercion');}});
      let answer;
      if (fail) assert.throws(() => fn(argument),error => error === original);
      else {answer = fn(argument); assert.equal(typeof answer,'boolean');}
      assert.equal(reads,compound && !fail && value !== null ? 2 : 1); assert.equal(coerces,0);
      return {answer,reads,coerces};
    };
    assert.deepEqual(observe(actual),observe(reference)); if (count) ++cases;
  }
  if (compound) for (const first of values) for (const second of values)
    for (const failAt of [0,1,2]) {
      const observe = fn => {
        let reads = 0, caught = false, answer;
        const original = Symbol('ordered getter failure');
        const argument = {get probe() {
          if (++reads === failAt) throw original;
          return reads === 1 ? first : second;
        }};
        try {answer = fn(argument); assert.equal(typeof answer,'boolean');}
        catch (error) {assert.equal(error,original); caught = true;}
        return {answer,reads,caught};
      };
      assert.deepEqual(observe(actual),observe(reference)); if (count) ++cases;
    }
};
for (const row of rows) verify(row);
assert.equal(rows.length,60);
let mutations = 0;
for (const mixed of [false,true]) {
  const row = rows.find(row => row.mixed === mixed && row.copies === 0);
  for (const source of [row.source.replace('value.probe','(value.probe, value.probe)'),
      row.source.replace(/return [^;]+;/,'return null;')]) {
    assert.notEqual(source,row.source);
    vm.runInNewContext(`(${source})`); // A rejected syntax change proves nothing.
    assert.throws(() => verify({...row,source},false)); ++mutations;
  }
}
const decision = rows.find(row => row.compound && row.copies === 0);
const retained = `function(value) { const probe = value.probe; return (${decision.source})({get probe() {return probe;}}); }`;
vm.runInNewContext(`(${retained})`);
assert.throws(() => verify({...decision,source:retained},false)); ++mutations;
console.log(JSON.stringify({decodedBooleanPhiCases:cases,profiles:3,copySplits:[0,200,201,402],mixedNull:true,convertedPredicate:true,compoundPredicate:true,mutations}));
