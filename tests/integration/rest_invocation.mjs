import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const fixtures=execFileSync(process.argv[2],['--rest-caught-json'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
assert.equal(fixtures.length,30);
let comparisons=0;
for(const fixture of fixtures) {
  assert.ok(!fixture.source.includes('INCOMPLETE'));
  const Recovered=Function(`'use strict';return class {${fixture.source}}`)();
  const method=fixture.static?Recovered.run:Recovered.prototype.run;
  assert.equal(method.length,1);
  assert.throws(()=>Reflect.construct(method,[]),TypeError);
  const marker={},failure={};
  for(const values of [[],[undefined],[marker],[marker,Symbol('arg'),-0],Array.from({length:201},(_,i)=>i===200?marker:i)]) {
    for(const throws of [false,true])for(const receiver of [undefined,null,{}]) {
      const observed=[];
      const callback=array=>{observed.push(array);if(throws)throw failure;};
      const actual=Reflect.apply(method,receiver,[callback,...values]);
      assert.equal(observed.length,1);assert.deepEqual(observed[0],values);
      assert.notEqual(observed[0],values);
      if(throws)assert.equal(actual,undefined);else assert.equal(actual,observed[0]);
      const next=Reflect.apply(method,receiver,[callback,...values]);
      assert.notEqual(observed[0],observed[1]);
      if(!throws)assert.equal(next,observed[1]);
      for(let i=0;i<values.length;++i)assert.ok(Object.is(observed[0][i],values[i]));
      ++comparisons;
    }
  }
  assert.equal(method(),undefined);++comparisons;
  let nested;
  const outer=method(array=>{nested=method(()=>{},marker);assert.notEqual(nested,array);},marker);
  assert.equal(outer[0],marker);assert.equal(nested[0],marker);++comparisons;
  // Array identity is observable, so a mutant that caches one invocation's
  // rest value cannot pass the same independent fresh-result obligation.
  const cached=method(()=>{},marker);
  const bad=()=>cached;
  assert.throws(()=>assert.notEqual(bad(),bad()),assert.AssertionError);
}
console.log(JSON.stringify({sources:fixtures.length,comparisons}));
