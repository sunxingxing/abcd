// A local class whose member captures the creating method's parameter: the
// requirement binds to the creator's actual storage, so the record completes.
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
const load=p=>import('data:text/javascript;base64,'+Buffer.from(readFileSync(p)).toString('base64'));
const dir=mkdtempSync(join(tmpdir(),'abcd-creator-classes-'));
try {
 const original=await load(fixture.replace(/\.abc$/,'.js'));
 const trace=m=>{const o=new m.Outer();const L1=o.factory(1),L2=o.factory('x');
  return [new L1().read(),new L2().read(),L1===L2,L1.name,typeof L1.prototype.read,Object.getOwnPropertyNames(m.Outer.prototype)];};
 for(const [name,flags] of [['default',[]],['source',['--language-source']]]) {
  const out=join(dir,name);run(structure,[fixture,out,'--value-source',...flags]);
  const rows=JSON.parse(readFileSync(join(out,'manifest.json'))).records;
  const row=rows.find(r=>r.source&&r.source_status===0);assert.ok(row);
  const metadata=JSON.parse(readFileSync(join(out,row.metadata)));
  const factory=metadata.methods.find(m=>m.runtime_name==='factory');assert.ok(factory);
  const cli=JSON.parse(run(semantic,[fixture,String(factory.entity),'--record-source',...flags]));
  const file=join(out,row.source);assert.equal(cli.source,readFileSync(file,'utf8'));
  assert.deepEqual(trace(await load(file)),trace(original));
 }
 console.log('creator_classes_cli_test: class-member capture of creator parameter completes, both contracts, Node differential');
} finally {rmSync(dir,{recursive:true,force:true});}
