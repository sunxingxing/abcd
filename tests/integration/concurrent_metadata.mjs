// Metadata, gates and actual exported-record recompilation. Node is not a taskpool oracle.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,input,probe,compiler]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-concurrent-evidence-'));
const run=(exe,args,status=0)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(r.error);assert.equal(r.status,status,r.stderr);return r.stdout;};
const cases=[[10719,11357,11135,10,1],[16446,16922,16756,10,0],[18164,18358,18536,10,1],[31477,31665,31637,21,0],[25688,25816,25849,17,1]];
try {
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
  const out=join(dir,label);run(structure,[input,out,'--value-source',...flags]);
  const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
  assert.equal(manifest.source_contract,label==='source'?'language_source_v3':'language_numeric_constructors_v1');
  for(const [entity,method,creator,pc,count] of cases) {
   const record=manifest.records.find(r=>r.entity===entity);
   if(label==='source' && [10719,18164,31477,25688].includes(entity)) {
    assert.equal(record.source_status,0);assert.ok(record.source);
    const source=readFileSync(join(out,record.source),'utf8');assert.ok(source.includes('"use concurrent";'));
    const cli=JSON.parse(run(semantic,[input,String(method),'--record-source',...flags]));assert.equal(cli.source_complete,true);assert.equal(cli.source,source);
    run(process.execPath,['--check',join(out,record.source)]);
    const before=JSON.parse(run(probe,[input]));
    const owned=before.find(r=>r.functions.some(f=>f.method===method));assert.ok(owned);
    const file=join(dir,`${entity}.js`),abc=join(dir,`${entity}.abc`);writeFileSync(file,source);
    const name=owned.record.slice(1,-1);
    run(compiler,['--module','--merge-abc','--opt-level=0',`--record-name=${name}`,`--source-file=${name}.js`,`--output=${abc}`,file]);
    const after=JSON.parse(run(probe,[abc])).find(r=>r.record===owned.record);assert.ok(after);
    for(const key of ['requests','request_phases','imports','namespace_imports','exports','indirect_exports','star_exports'])assert.deepEqual(after[key],owned[key],`${entity} ${key}`);
    const summarize=f=>({name:f.name,kind:f.kind,sendable:f.sendable,annotation_present:f.annotation_present,requests:f.requests});
    assert.deepEqual(after.functions.map(summarize).sort((a,b)=>a.name.localeCompare(b.name)),owned.functions.map(summarize).sort((a,b)=>a.name.localeCompare(b.name)));
    if(entity===10719) {
      const a=after.functions.find(f=>f.name==='updateBooks');
      assert.equal(a.async_protocol,true);assert.equal(a.completion_verified,true);assert.equal(a.async_owner_verified,true);
      assert.equal(a.resolve_ports,1);assert.equal(a.reject_ports,1);assert.deepEqual(a.requests,owned.functions.find(f=>f.name==='updateBooks').requests);
      assert.deepEqual(a.requests.map(q=>after.requests.indexOf(q)),[1]);
    }
    const reexport=join(dir,`${entity}-proof`);run(structure,[abc,reexport,'--value-source','--language-source']);
    // Recompilation may expose other unsupported class metadata/body forms.
    // The standalone fixture separately requires a complete second export.
    continue;
   }
   assert.equal(record.source_status,3);assert.equal(record.source,undefined);
   const f=record.source_failure;
   const cli=JSON.parse(run(semantic,[input,String(method),'--record-source',...flags],1));assert.deepEqual(cli.source_failure,f);
   if(label==='old' && entity===25688) {assert.equal(f.causes.at(-1).domain,'abcd::source::ScopeError');assert.equal(f.concurrent,undefined);continue;}
   if(label==='source' && entity===16446) {assert.equal(f.method,16784);assert.deepEqual(f.causes,[{domain:'abcd::source::PhasedMethodError',value:2}]);continue;}
   assert.equal(f.class,'unproved');assert.equal(f.method,method);assert.equal(f.pc,label==='source'?10:undefined);assert.equal(f.working_pc,label==='source'?10:undefined);
   assert.equal(f.creation_method,creator);assert.equal(f.creation_pc,pc);
   assert.equal(f.concurrent.kind,label==='source'?7:0);assert.equal(f.concurrent.method,method);assert.equal(f.concurrent.record,entity);
   assert.equal(f.concurrent.creator,creator);assert.equal(f.concurrent.creation_pc,pc);
   assert.equal(f.concurrent.annotation_present,true);assert.equal(f.concurrent.request_count,count);assert.ok(f.concurrent.annotation);
   assert.deepEqual(f.causes,[{domain:'abcd::source::CreationContextError',value:3},{domain:'abcd::source::ConcurrentError',value:label==='source'?7:0}]);
  }
 }
 console.log('5 records x 2 contracts: old policy/Mock capture unchanged; async promise/notify ports; collectFrame next environment refusal; 4 exported records round-trip kind/sendable/ordered requests/module tables');
} finally {rmSync(dir,{recursive:true,force:true});}
