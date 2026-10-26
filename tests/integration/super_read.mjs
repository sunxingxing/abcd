import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';import {tmpdir} from 'node:os';
const [structure,fixtures,semantic]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-super-'));
try{
 const evaluate=file=>{const r=spawnSync(process.execPath,[join(fixtures,'harness.mjs'),file],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);return r.stdout;};
 for(const name of ['normal','phased']){
  const original=join(temp,name+'.mjs');copyFileSync(join(fixtures,name+'.js'),original);const oracle=evaluate(original);
  for(const flags of [[], ['--language-source']]){
   const output=join(temp,name+flags[0]);const abc=join(fixtures,name+'.abc');
   const r=spawnSync(structure,[abc,output,'--value-source',...flags],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);
   const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status!==2);
   assert.equal(record.source_status,0,JSON.stringify(record));
   const metadata=JSON.parse(readFileSync(join(output,record.metadata)));const method=metadata.methods.find(m=>m.runtime_name==='run');
   const decoded=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(decoded.status,0,decoded.stderr);
   const ops=decoded.stdout.trim().split('\n').map(JSON.parse);const del=ops.filter(i=>i.opcode===0x46 || i.opcode===0x92);
   assert.equal(del.length,1);assert.equal(del[0].exceptional_completion,1);assert.equal(del[0].exception_edge.propagates_to_caller,name==='normal'?true:undefined);
   if(name==='phased')assert.ok(Number.isInteger(del[0].exception_edge.handler_pc));
   const source=join(output,record.source);assert.equal(evaluate(source),oracle);
   const text=readFileSync(source,'utf8');assert.match(text,/super\.value/);
   const mutation=join(temp,'mutated.mjs');writeFileSync(mutation,text.replaceAll('super.value','this.value'));assert.notEqual(evaluate(mutation),oracle,'receiver lookup instead of home lookup must be detected');
  }
 }
}finally{rmSync(temp,{recursive:true,force:true});}
