// Typed omissions are additional diagnostics; a fully recovered member removes its gap.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const inputs=new Map([[8, "DocsSample/InputMethod/KikaInputMethod/artifacts/entry/default/default/modules/modules.abc"], [20, "UI/CustomAnimationTab/artifacts/entry/default/default/modules/modules.abc"]].map(([i,path])=>[i,join(root,path)]));
const dir=mkdtempSync(join(tmpdir(),'abcd-class-omissions-'));
const cases=[
 [8,10284,11969,{constructor:12788,member:12733,method:11580,pc:49,working_pc:49,creation_method:12733,creation_pc:224}],
 [20,10622,12066,{constructor:11284,member:11067,method:11067,pc:633,working_pc:633}],
];
const run=(tool,args)=>{const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.status,0,p.stderr);return p;};
try{
 for(const flags of [['--language-numeric','--language-constructors'],['--language-source']])
 for(const [cohort,record,entry,witness] of cases){
  const abc=inputs.get(cohort),out=join(dir,`${cohort}-${flags.length}`);
  run(structure,[abc,out,'--value-source',...flags]);
  const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.entity===record);
  const cli=JSON.parse(run(semantic,[abc,String(entry),'--record-source',...flags]).stdout);
  if(cohort===20) {
   // Round 64 proves the loop-carried cell initialization; aboutToAppear is
   // now recovered, so this formerly partial class must be complete.
   assert.equal(row.source_status,0);assert.equal(cli.source_complete,true);
   assert.equal(row.source_omitted_members,undefined);assert.equal(cli.source_omitted_members,undefined);
   assert.equal(cli.source,readFileSync(join(out,row.source),'utf8'));
   assert.ok(cli.source.includes('aboutToAppear() {'));continue;
  }
  if(cohort===8) {
   // Since 2026-10-23 a phased creator (registerListener has a try region)
   // hands its creation-time initialized module slots to its closures, so the
   // unguarded enum read in the setSubtype arrow is proved and the class completes.
   assert.equal(row.source_status,0);assert.equal(cli.source_complete,true);
   assert.equal(row.source_omitted_members,undefined);assert.equal(cli.source_omitted_members,undefined);
   assert.equal(cli.source,readFileSync(join(out,row.source),'utf8'));
   assert.ok(cli.source.includes('CustomInputMethodSubtype.english'));continue;
  }
  const internal_value={domains:[0,1,2,3,4],observer_instruction:20,producer_instruction:19,
   producer_kind:1,read_ordinal:0,reason:6,value:53};
  const expected=[{...witness,internal_value,causes:[{domain:'abcd::analysis::SemanticProofError',value:3},{domain:'abcd::analysis::InternalValueReason',value:6}]}];
  assert.equal(row.source_status,4);assert.equal(row.source_complete,false);assert.equal(cli.source_complete,false);
  assert.deepEqual(row.source_omitted_members,expected);assert.deepEqual(cli.source_omitted_members,expected);
  assert.equal(cli.source,readFileSync(join(out,row.incomplete_source),'utf8'));
  assert.equal(row.source_failure,undefined);assert.equal(cli.source_failure,undefined);
  assert.ok(cli.source.includes('INCOMPLETE class: unrecovered members'));
  const dis=run(semantic,[abc,String(witness.method),'--instructions']);
  assert.ok(dis.stdout.trim().split('\n').map(JSON.parse).some(i=>i.pc===witness.pc));
 }
 console.log('class_omissions_cli_test: both formerly partial records complete');
}finally{rmSync(dir,{recursive:true,force:true});}
