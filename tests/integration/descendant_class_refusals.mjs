import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const run=(tool,args,status=0)=>{const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000});assert.ifError(p.error);assert.equal(p.status,status,p.stderr);return p.stdout;};
const dir=mkdtempSync(join(tmpdir(),'abcd-class-refusals-'));
try{
 const family=reason=>[{domain:'abcd::source::FamilyFileError',value:1},{domain:'abcd::source::FamilyCreationCheck',value:reason}];
 // Derived classes are no longer refused wholesale in descendant creators;
 // `class extends Base {}` now stops at its implicit constructor's entry ABI.
 // A member reading a later `let` now completes: its creator declares the
 // deferred cell after the class, keeping the TDZ (checked by running it).
 for(const label of ['default','source']){
  const out=join(dir,'deferred-complete-'+label);
  run(structure,[join(fixtures,'deferred.abc'),out,'--value-source',...(label==='source'?['--language-source']:[])]);
  const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(row.source_status,0,JSON.stringify(row));
  const probe=spawnSync(process.execPath,['--input-type=module','-e',`const m=await import(${JSON.stringify(join(out,row.source))});const C=m.factory();console.log(new C().read());`],{encoding:'utf8'});
  assert.equal(probe.stdout.trim(),'1',probe.stderr);
 }
 for(const [name,causes,located] of [['heritage',[{domain:'abcd::source::FunctionAbiError',value:2}],false],['nested',family(19),true]]){ // member completes (creator_classes_cli_test)
  for(const [label,flags] of [['default',[]],['source',['--language-source']]]){
   const abc=join(fixtures,name+'.abc'),out=join(dir,name+'-'+label);
   run(structure,[abc,out,'--value-source',...flags]);
   const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
   assert.equal(row.source_status,3,JSON.stringify(row));const f=row.source_failure;
   assert.deepEqual(f.causes,causes);
   const cli=JSON.parse(run(semantic,[abc,String(f.method),'--record-source',...flags],1));assert.deepEqual(cli.source_failure,f);
   if(!located)continue;
   assert.ok(Number.isInteger(f.pc));assert.ok(Number.isInteger(f.family_creation.location.operation));
   const instructions=run(semantic,[abc,String(f.method),'--instructions']).trim().split('\n').map(JSON.parse);
   assert.ok(instructions.some(i=>i.pc===f.pc && i.class_creation));
  }
 }
 console.log('descendant_class_refusals_cli_test: deferred completes; heritage/nested refusals, real locations, both JSON paths');
}finally{rmSync(dir,{recursive:true,force:true});}
