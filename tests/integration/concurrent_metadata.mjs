// No execution oracle: this increment only publishes metadata evidence.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,input]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-concurrent-evidence-'));
const run=(exe,args,status=0)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(r.error);assert.equal(r.status,status,r.stderr);return r.stdout;};
const cases=[[10719,11357,11135,10,1],[16446,16922,16756,10,0],[18164,18358,18536,10,1],[31477,31665,31637,21,0],[25688,25816,25849,17,1]];
try {
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
  const out=join(dir,label);run(structure,[input,out,'--value-source',...flags]);
  const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
  assert.equal(manifest.source_contract,label==='source'?'language_source_v3':'language_numeric_constructors_v1');
  for(const [entity,method,creator,pc,count] of cases) {
   const record=manifest.records.find(r=>r.entity===entity);assert.equal(record.source_status,3);assert.equal(record.source,undefined);
   const f=record.source_failure;
   const cli=JSON.parse(run(semantic,[input,String(method),'--record-source',...flags],1));assert.deepEqual(cli.source_failure,f);
   if(label==='old' && entity===25688) {assert.equal(f.causes.at(-1).domain,'abcd::source::ScopeError');assert.equal(f.concurrent,undefined);continue;}
   assert.equal(f.class,'unproved');assert.equal(f.method,method);assert.equal(f.pc,undefined);assert.equal(f.working_pc,undefined);
   assert.equal(f.creation_method,creator);assert.equal(f.creation_pc,pc);
   assert.equal(f.concurrent.kind,label==='source'?1:0);assert.equal(f.concurrent.method,method);assert.equal(f.concurrent.record,entity);
   assert.equal(f.concurrent.creator,creator);assert.equal(f.concurrent.creation_pc,pc);
   assert.equal(f.concurrent.annotation_present,true);assert.equal(f.concurrent.request_count,count);assert.ok(f.concurrent.annotation);
   assert.deepEqual(f.causes,[{domain:'abcd::source::CreationContextError',value:3},{domain:'abcd::source::ConcurrentError',value:label==='source'?1:0}]);
  }
 }
 console.log('concurrent metadata: 5 records x 2 contracts; policy/preparation gates, exact locations, CLI/manifest agreement; old Mock capture refusal unchanged');
} finally {rmSync(dir,{recursive:true,force:true});}
