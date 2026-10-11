import assert from 'node:assert/strict';
import {generatedSource} from '../support/source/generated_fixture.mjs';

const rows=generatedSource(process.argv[2],['--source'],{encoding:'utf8',maxBuffer:16_000_000})
  .trim().split('\n').map(JSON.parse);
assert.equal(rows.length,17);
const values=[undefined,null,false,true,0,-0,7,NaN,Infinity,0n,Symbol('value'),{},[],
  {[Symbol.toPrimitive](){throw new Error('guard coerced a language value');}}];
const failure=Symbol('callback failure');
let comparisons=0,mutants=0;
function run(fn,value,throws) {
  const trace=[];
  const result=fn(value,()=>{trace.push('callback');if(throws)throw failure;});
  return {trace,result};
}
const reference=(value,callback)=>{let local=value;try{const held=local;callback();return held;}catch(error){return error;}};
for(const row of rows) {
  const fn=Function('return ('+row.source+')')();
  for(const value of values)for(const throws of [false,true]) {
    const actual=run(fn,value,throws),expected=run(reference,value,throws);
    assert.deepEqual(actual.trace,expected.trace);assert(Object.is(actual.result,expected.result));++comparisons;
  }
  const outer={},inner={};let reentered;
  assert.equal(fn(outer,()=>{reentered=fn(inner,()=>{});}),outer);
  assert.equal(reentered,inner);comparisons+=2;
  // Native undefined is initialized. Reintroducing a truth/undefined guard
  // must fail, as must losing the callback's catch.
  const wrongGuard=(value,callback)=>{if(value===undefined)throw failure;return reference(value,callback);};
  assert.throws(()=>assert.deepEqual(run(wrongGuard,undefined,false),run(reference,undefined,false)));++mutants;
  const missingCatch=(value,callback)=>{callback();return value;};
  assert.throws(()=>assert.deepEqual(run(missingCatch,outer,true),run(reference,outer,true)));++mutants;
}
console.log(JSON.stringify({sources:rows.length,comparisons,mutants}));
