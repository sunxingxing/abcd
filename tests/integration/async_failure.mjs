// Round 69 diagnostic acceptance; corpus bytecode is input, paired source is not.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {traceAsyncMemberForOf} from '../support/for_of_async_oracle.mjs';
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
   const r=manifests.get(n).find(r=>r.entity===record);
   // Round 78: raw catch-input coverage closes these two previous refusals.
   if(n===9||n===39){
    assert.equal(r.source_status,0);assert.equal(r.source_complete,true);assert.ok(r.source);
    const cli=JSON.parse(run(semantic,[inputs.get(n),String(method),'--record-source',...flags]));
    assert.equal(cli.source_complete,true);assert.equal(cli.source,readFileSync(join(dir,label+n,r.source),'utf8'));continue;
   }
   if(record===23058 && label==='source') {
    assert.equal(r.source_status,0);assert.equal(r.source_complete,true);assert.ok(r.source);
    const cli=JSON.parse(run(semantic,[inputs.get(n),String(method),'--record-source',...flags]));
    const file=join(dir,label+n,r.source);assert.equal(cli.source_complete,true);assert.equal(cli.source,readFileSync(file,'utf8'));
    run(process.execPath,['--check',file]);
    const actual=await import(pathToFileURL(file));
    const oracle=readFileSync(join(dirname(inputs.get(n)),'src/oh_modules/.ohpm/@ohos+hypium@1.0.19/oh_modules/@ohos/hypium/src/main/event.js'),'utf8');
    const expected=await import('data:text/javascript;base64,'+Buffer.from(oracle).toString('base64'));
    for(const [name,methods] of [['SpecEvent',['specStart','specDone']],['SuiteEvent',['suiteStart','suiteDone']],['TaskEvent',['taskStart','taskDone']]])for(const member of methods) {
      const invoke=module=>values=>{const instance=new module[name]({id:1,context:{},coreContext:{}});instance.eventMonitors=values;return instance[member]();};
      for(const count of [0,1,3])for(const fail of ['none','getter','throw','reject'])for(const close of ['object','getter','call','null','primitive'])
        assert.deepEqual(await traceAsyncMemberForOf(invoke(actual),count,fail,close,member),await traceAsyncMemberForOf(invoke(expected),count,fail,close,member),`${name}.${member}/${count}/${fail}/${close}`);
    }
    continue;
   }
   assert.equal(r.source_status,3);assert.equal(r.source,undefined);
   // The input-register proof authorizes no effectful IteratorClose projection.
   if(record===23058){
    const f=r.source_failure;assert.equal(f.method,23306);assert.equal(f.pc,149);assert.equal(f.working_pc,125);
    assert.deepEqual(f.causes,[{domain:'abcd::source::PhasedMethodError',value:6},{domain:'abcd::source::ExceptionControlError',value:4}]);
    assert.equal(f.exception_region.region,0);assert.equal(f.exception_region.body_pc,149);assert.equal(f.exception_region.handler_pc,174);
    assert.equal(f.exception_region.exits.length,2);
    const cli=JSON.parse(run(semantic,[inputs.get(n),String(method),'--record-source',...flags],1));assert.deepEqual(cli.source_failure,f);continue;
   }
   if(record===58223){
    // Since 2026-10-23 the hoisted `done` creation before ASYNCFUNCTIONENTER is
    // an admitted prelude and its multi-store `timer` capture has a dominating
    // declaring store. Since the for-of body-scope slice (also 2026-10-23)
    // m61706 completes under --language-source (the for-of route needs the
    // external-exception premise); the record then stops at async m61057.
    const f=r.source_failure;assert.equal(f.class,'unproved');
    if(label==='source'){assert.equal(f.method,61057);assert.equal(f.pc,192);assert.equal(f.working_pc,168);}
    else {assert.equal(f.method,61706);assert.equal(f.pc,643);}
    assert.deepEqual(f.causes,[{domain:'abcd::source::PhasedMethodError',value:6},{domain:'abcd::source::ExceptionControlError',value:4}]);
    const rows=run(semantic,[inputs.get(n),String(method),'--instructions']).trim().split('\n').map(JSON.parse);
    const actual=rows.find(r=>r.pc===pc);assert.ok(actual);assert.equal(actual.opcode,0x33);assert.equal(actual.operands[1].method_entity,59227);
    continue;
   }
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
 console.log('async failures: two prior completions, event completes under source with 360 Node cases, old-contract close and prelude refusals unchanged; CLI/manifest equality');
}finally{rmSync(dir,{recursive:true,force:true});}
