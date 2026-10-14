// Increment I is a typed discovery gate, never executable partial class source.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,fixture]=process.argv.slice(2);
const run=(tool,args,status=0)=>{
 const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
 assert.ifError(p.error);assert.equal(p.status,status,p.stderr);return p.stdout;
};
const dir=mkdtempSync(join(tmpdir(),'abcd-creator-classes-'));
try {
 for(const [name,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
  const out=join(dir,name);run(structure,[fixture,out,'--value-source',...flags]);
  const rows=JSON.parse(readFileSync(join(out,'manifest.json'))).records;
  const row=rows.find(r=>r.source_status===3);assert.ok(row);
  const metadata=JSON.parse(readFileSync(join(out,row.metadata)));
  const factory=metadata.methods.find(m=>m.runtime_name==='factory');assert.ok(factory);
  const instructions=run(semantic,[fixture,String(factory.entity),'--instructions']).trim().split('\n').map(JSON.parse);
  const creation=instructions.find(i=>i.class_creation);assert.ok(creation);
  const cli=JSON.parse(run(semantic,[fixture,String(factory.entity),'--record-source',...flags],1));
  assert.deepEqual(cli.source_failure,row.source_failure);
  const f=row.source_failure;
  assert.deepEqual(f.causes,[{domain:'abcd::source::FamilyFileError',value:1},{domain:'abcd::source::FamilyCreationCheck',value:15}]);
  assert.equal(f.method,factory.entity);assert.equal(f.pc,creation.pc);
  assert.equal(f.family_creation.occurrence,0); // A class-member family root is still a descendant creator.
  assert.equal(f.family_creation.root,factory.entity);
  assert.equal(f.family_creation.constructor_pool,creation.class_creation.constructor_pool);
  assert.equal(f.family_creation.literal_pool,creation.class_creation.literal_pool);


  assert.equal(f.family_creation.location.operation,f.family_creation.projected_class);
  assert.equal(row.executable,false);assert.equal(row.source,undefined);assert.equal(row.incomplete_source,undefined);
 }
 console.log('creator_classes_cli_test: class-member capture requirement, both contracts, no publication, manifest/CLI agree');
} finally {rmSync(dir,{recursive:true,force:true});}
