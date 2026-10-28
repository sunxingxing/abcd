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
 for(const [label,flags] of [['default',[]],['source',['--language-source']]]){
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
   if(record===23058) {
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
   if(record===58223){
    // A failed refinement must retain the independently omittable member and
    // the rest of the verified record. The new typed cause stays diagnostic.
    assert.equal(r.source_status,4);assert.equal(r.source_complete,false);
    assert.equal(r.source,undefined);assert.ok(r.incomplete_source);
    assert.equal(r.source_failure,undefined);
    assert.equal(r.source_omitted_members.length,1);
    const omission=r.source_omitted_members[0];
    assert.equal(omission.member,60551);assert.equal(omission.method,60551);
    assert.equal(omission.pc,313);assert.equal(omission.working_pc,310);
    assert.deepEqual(omission.causes,[
      {domain:'abcd::source::PhasedMethodError',value:6},
      {domain:'abcd::source::ExceptionControlError',value:4},
      {domain:'abcd::source::CompletionPortError',value:9},
      {domain:'abcd::source::ForOfIterationError',value:3}]);
    const failure=omission.later_failure;
    assert.equal(failure.location.method,60551);
    assert.equal(failure.location.pc,2208);assert.equal(failure.location.working_pc,1512);
    assert.deepEqual(failure.causes,[
      {domain:'abcd::source::FunctionTreeError',value:1},
      {domain:'abcd::source::ExpressionError',value:4},
      {domain:'abcd::source::CallPlanError',value:4},
      {domain:'abcd::source::CallReferenceError',value:1},
      {domain:'abcd::source::EvaluationKind',value:1},
      {domain:'abcd::source::TemporaryReason',value:4}]);
    assert.deepEqual(failure.call_reference,{call:619,kind:1,placement:1,property:588,temporary:4});
    const cli=JSON.parse(run(semantic,[inputs.get(n),String(method),'--record-source',...flags]));
    assert.equal(cli.source_complete,false);assert.deepEqual(cli.source_omitted_members,r.source_omitted_members);
    const file=join(dir,label+n,r.incomplete_source);
    assert.equal(cli.source,readFileSync(file,'utf8'));run(process.execPath,['--check',file]);
    assert.match(cli.source,/UNRECOVERED class member/);continue;
   }
   assert.equal(r.source_status,3);assert.equal(r.source,undefined);

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
 console.log('async failures: two prior completions, event completes under source with 360 Node cases, preserved service partial output plus later typed refusal; default/alias CLI/manifest equality');
}finally{rmSync(dir,{recursive:true,force:true});}
