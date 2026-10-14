// Typed omissions are additional diagnostics. The class and record stay partial.
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
  const expected=[{...witness,causes:[{domain:'abcd::analysis::SemanticProofError',value:3}]}];
  assert.equal(row.source_status,4);assert.equal(row.source_complete,false);assert.equal(cli.source_complete,false);
  assert.deepEqual(row.source_omitted_members,expected);assert.deepEqual(cli.source_omitted_members,expected);
  assert.equal(cli.source,readFileSync(join(out,row.incomplete_source),'utf8'));
  assert.equal(row.source_failure,undefined);assert.equal(cli.source_failure,undefined);
  assert.ok(cli.source.includes('INCOMPLETE class: unrecovered members'));
  const dis=run(semantic,[abc,String(witness.method),'--instructions']);
  assert.ok(dis.stdout.trim().split('\n').map(JSON.parse).some(i=>i.pc===witness.pc));
 }
 console.log('class_omissions_cli_test: 4 partial records preserve typed member/descendant locations across both outputs');
}finally{rmSync(dir,{recursive:true,force:true});}
