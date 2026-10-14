// Round 69 diagnostic acceptance; corpus bytecode is input, paired source is not.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const inputs=new Map([
 [9,'DocsSample/ArkData/RelationalStore/DataSyncAndPersistence/artifacts/entry/default/default/modules/modules.abc'],
 [32,'DocsSample/ArkTS/NodeAPI/NodeAPIClassicUseCases/NodeAPILoadModuleWithInfo/artifacts/entry/default/default/modules/modules.abc'],
 [39,'DocsSample/ArkWeb/ArkWebMenu/artifacts/entry/default/default/modules/modules.abc']
].map(([n,p])=>[n,join(root,p)]));
const cases=[[9,7596,7950,3459,3537,25,true],[32,23058,23306,99,131,13,false],
 [32,58223,58418,18],[39,21472,22136,75,348,11,true]];
const dir=mkdtempSync(join(tmpdir(),'abcd-async-failure-'));
const run=(exe,args,status=0)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(r.error);assert.equal(r.status,status,r.stderr);return r.stdout;};
try{
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
  const manifests=new Map();
  for(const [n,input] of inputs){const out=join(dir,label+n);run(structure,[input,out,'--value-source',...flags]);manifests.set(n,JSON.parse(readFileSync(join(out,'manifest.json'))).records);}
  for(const [n,record,method,pc,handler,slot,confined] of cases){
   const r=manifests.get(n).find(r=>r.entity===record);assert.equal(r.source_status,3);assert.equal(r.source,undefined);
   const f=r.source_failure;assert.equal(f.class,'unproved');assert.equal(f.method,method);assert.equal(f.pc,pc);
   assert.equal(f.working_pc,undefined);assert.deepEqual(f.async.location,{method,pc});
   const expected=[{domain:'abcd::source::FunctionEmitError',value:1},
     {domain:'abcd::source::AsyncCompletionCheck',value:handler===undefined?1:20}];
   if(handler!==undefined){expected.push({domain:'abcd::analysis::AwaitCatchError',value:1});
    assert.deepEqual(f.async.catch_input,{handler_pc:handler,register:slot,prefix:0,suffix_known:true,suffix_confined:confined});
   }else assert.equal(f.async.catch_input,undefined);
   assert.deepEqual(f.causes,expected);
   const cli=JSON.parse(run(semantic,[inputs.get(n),String(method),'--record-source',...flags],1));
   assert.deepEqual(cli.source_failure,f);
   const rows=run(semantic,[inputs.get(n),String(method),'--instructions']).trim().split('\n').map(JSON.parse);
   const actual=rows.find(r=>r.pc===pc);assert.ok(actual);
   if(handler!==undefined){assert.equal(actual.coroutine_transfer.kind,0);assert.ok(rows.some(r=>r.pc===handler));}
   else{assert.equal(actual.opcode,0x33);assert.equal(actual.operands[1].method_entity,59227);}
  }
 }
 console.log('async failures: four first refusals x two contracts; exact check, original PC, handler/register, CLI/manifest equality');
}finally{rmSync(dir,{recursive:true,force:true});}
