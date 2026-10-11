import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const rows=execFileSync(process.argv[2],['--preserved-completions-json'],
  {encoding:'utf8',timeout:120_000,maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(rows.length,18);
let comparisons=0,mutants=0;
for(const row of rows) {
  const capture=Function('return ('+row.source+')')(),children=[],initial=[{},Symbol('second')];
  let iteration=0;
  capture(()=>iteration<2,()=>{const value=initial[iteration++];if(row.caught)throw value;return value;},
    (get,set)=>children.push({get,set}));
  assert.equal(children.length,2);
  if(row.kind===0) {
    const resumes=[],pending=children.map(({get})=>get(new Promise(resolve=>resumes.push(resolve))));
    const changed={};children[0].set(changed);resumes[1]();resumes[0]();
    assert.deepEqual(await Promise.all(pending),[changed,initial[1]]);++comparisons;
    // Reading before suspension would lose the sibling's write.
    assert.notEqual(changed,initial[0]);++mutants;
    for(const payload of [undefined,null,false,0,-0,NaN,Symbol('throw'),{}]) {
      const pending=children[0].get(Promise.reject(payload));
      await assert.rejects(pending,error=>Object.is(error,payload));++comparisons;
    }
  } else if(row.kind===1) {
    for(const payload of [undefined,null,false,0,-0,NaN,Symbol('pending'),{}])
      for(const reject of [false,true])for(const cleanupReject of [false,true]) {
        async function observe(recovered) {
          const child=children[0],before={},during={},after={},failure={},trace=[];
          let current=before;child.set(before);
          const change=value=>{current=value;child.set(value);};
          const oracle=async(input,cleanup)=>{try{await input;}finally{await cleanup(current);}return current;};
          const input={then(resolve,fail){trace.push('input');change(during);(reject?fail:resolve)(payload);}};
          const cleanup=value=>{assert.equal(value,during);trace.push('cleanup');return {
            then(resolve,fail){trace.push('cleanup-resume');change(after);(cleanupReject?fail:resolve)(failure);}
          };};
          let result,error,thrown=false;
          try{result=await(recovered?child.get:oracle)(input,cleanup);}catch(caught){error=caught;thrown=true;}
          assert.deepEqual(trace,['input','cleanup','cleanup-resume']);
          assert.equal(thrown,reject||cleanupReject);
          if(thrown)assert(Object.is(error,cleanupReject?failure:payload));else assert.equal(result,after);
          return {trace,thrown,result:thrown?undefined:'after',error:thrown?(cleanupReject?'cleanup':'input'):undefined};
        }
        assert.deepEqual(await observe(true),await observe(false));++comparisons;
      }
    // A cleanup failure must replace the pending completion.
    const first={},second={};
    await assert.rejects(children[0].get(Promise.reject(first),()=>Promise.reject(second)),error=>error===second);++mutants;
  } else {
    for(const payload of [undefined,null,false,0,-0,NaN,Symbol('pending'),{}])
      for(const failed of [false,true])for(const request of ['next','throw','return']) {
        function observe(recovered) {
          const trace=[],yielded={},injected={};
          const work=()=>{trace.push('work');if(failed)throw payload;return payload;};
          const cleanup=()=>{trace.push('cleanup');return yielded;};
          const oracle=function*(work,cleanup){try{return work();}finally{yield cleanup();}};
          const iterator=(recovered?children[0].get:oracle)(work,cleanup);
          assert.deepEqual(iterator.next(),{done:false,value:yielded});
          let result,error,thrown=false;
          try{result=iterator[request](injected);}catch(caught){error=caught;thrown=true;}
          assert.equal(thrown,request==='throw'||request==='next'&&failed);
          if(thrown)assert(Object.is(error,request==='throw'?injected:payload));
          else{assert(result.done);assert(Object.is(result.value,request==='return'?injected:payload));}
          assert.deepEqual(iterator.next(),{done:true,value:undefined});
          return {trace,thrown};
        }
        assert.deepEqual(observe(true),observe(false));++comparisons;
      }
    const trace=[],iterator=children[0].get(()=>trace.push('work'),()=>trace.push('cleanup'));
    iterator.return();assert.deepEqual(trace,[]);++comparisons;
  }
}
console.log(JSON.stringify({families:rows.length,comparisons,adverseChecks:mutants}));
