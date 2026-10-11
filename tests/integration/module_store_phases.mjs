import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
const source=execFileSync(process.argv[2],['source'],{encoding:'utf8'});
const context=vm.createContext({});
const recovered=new vm.SourceTextModule(`export let exported=0; export const run=(${source});`,{context});
const reference=new vm.SourceTextModule(`export let exported=0; export function run(next) {
  try {exported=next();exported=next();exported=next();} catch(error) {}
}`,{context});
for(const module of [recovered,reference]){await module.link(()=>{throw Error('unexpected import');});await module.evaluate();}
let scenarios=0;
for(const fail of [-1,0,1,2])for(const value of [undefined,null,false,37,NaN,{identity:true}]) {
  const traces=[];
  for(const module of [recovered,reference]) {
    module.namespace.run(()=>0);
    const trace=[];let ordinal=0;const failure={failure:true};
    const result=module.namespace.run(()=>{
      trace.push([ordinal,module.namespace.exported]);
      if(ordinal++===fail)throw failure;
      return value;
    });
    assert.equal(module.namespace.exported,fail===0?0:value);
    traces.push({trace,result,stored:module.namespace.exported});
  }
  assert.deepEqual(traces[0],traces[1]);++scenarios;
}
// The callback observes old module state before each successful write. Reentry
// changes the same live unit; the outer success must still publish its own RHS.
for(const module of [recovered,reference]) {
  module.namespace.run(()=>0);let calls=0;
  module.namespace.run(()=>{if(calls++===0)module.namespace.run(()=>99);return calls;});
  assert.equal(module.namespace.exported,3);++scenarios;
}
console.log(JSON.stringify({scenarios,sourceBytes:Buffer.byteLength(source)}));
