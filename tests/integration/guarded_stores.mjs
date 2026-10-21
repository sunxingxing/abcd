import {recordNodeEvidence} from '../support/isa/oracle_evidence.mjs';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,fixtures]=process.argv.slice(2),dir=mkdtempSync(join(tmpdir(),'abcd-guarded-stores-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:120000});assert.ifError(p.error);return p;};
try{
 const output=join(dir,'out'),exported=run(structure,[join(fixtures,'stores.abc'),output,'--value-source','--language-source']);assert.equal(exported.status,0,exported.stderr);
 const row=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status!==2);assert.equal(row.source_status,0,JSON.stringify(row));
 const emitted=join(output,row.source),text=readFileSync(emitted,'utf8');assert.equal(run(process.execPath,['--check',emitted]).status,0);
 const original=join(dir,'original.mjs');copyFileSync(join(fixtures,'stores.js'),original);
 const runner=join(dir,'runner.mjs');writeFileSync(runner,`
 globalThis.trace=[];const mode=process.argv[3];
 globalThis.prepare=next=>{trace.push(['rhs',next?.tag]);if(mode==='rhs')throw new RangeError();return mode==='undefined'?undefined:next;};
 if(mode==='lookup')Object.defineProperty(globalThis,'prepare',{get(){trace.push(['lookup']);throw new SyntaxError();},configurable:true});
 const m=await import(process.argv[2]);
 const same=m.makeWriter();const a={tag:'first'},b={tag:'second'};const first=same(a);const observed=m.observe();const second=m.writer(b);
 console.log(JSON.stringify({early:m.early,earlyRead:m.earlyRead,late:m.late,first,observed,second,after:m.observe(),firstIdentity:first===a,secondIdentity:second===b,name:same.name,length:same.length,trace}));
 `);
 let count=0;for(const mode of ['none','rhs','undefined','lookup']){
  const a=run(process.execPath,[runner,original,mode]),b=run(process.execPath,[runner,emitted,mode]);assert.equal(a.status,0,a.stderr);assert.equal(b.status,0,b.stderr);assert.equal(b.stdout,a.stdout,mode);++count;
 }
 const capture=/let (\$local\d+) = \{tag: "initial"\}/.exec(text)?.[1];assert.ok(capture,text);
 const guard=`${capture};\n`;assert.ok(text.includes(guard));
 const mutations=[
  text.replace(guard,''),
  `let ${capture};\n`+text.replace(`let ${capture} = {tag: "initial"}`,`${capture} = {tag: "initial"}`),
  text.replace(`${capture} = $value1`, `${capture} = {tag: "wrong"}`),
  text.replace('globalThis.prepare($arg0)', '(globalThis.trace.push("reordered"), globalThis.prepare($arg0))'),
 ];
 // Removing only the explicit guard retains PutValue's native TDZ check; that
 // mutation is intentionally equivalent and must NOT be called detected.
 const equivalent=join(dir,'equivalent.mjs');writeFileSync(equivalent,mutations[0]);
 assert.equal(run(process.execPath,[runner,equivalent,'none']).stdout,run(process.execPath,[runner,original,'none']).stdout);
 for(const [i,mutant] of mutations.slice(1).entries()){
  assert.notEqual(mutant,text);const file=join(dir,`mutant${i}.mjs`);writeFileSync(file,mutant);
  const a=run(process.execPath,[runner,original,'none']),b=run(process.execPath,[runner,file,'none']);assert.ok(b.status!==0||a.stdout!==b.stdout,'undetected mutation '+i);
 }
 recordNodeEvidence({abc:join(fixtures,'stores.abc'),output,original,producer:import.meta.url,cases:count,engine:structure});
 console.log(`guarded_stores: ${count} Node scenarios; 3 observed mutations; native-TDZ equivalent control`);
}finally{rmSync(dir,{recursive:true,force:true});}
