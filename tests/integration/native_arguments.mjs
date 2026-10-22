import {recordNodeEvidence} from '../support/isa/oracle_evidence.mjs';
// ABC-only engine input. Source is an independent differential oracle.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [engine,fixtures]=process.argv.slice(2);
const root=mkdtempSync(join(tmpdir(),'abcd-native-arguments-'));
try {
 const out=join(root,'source');execFileSync(engine,[join(fixtures,'entry.abc'),out,'--value-source','--language-source']);
 const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
 assert.equal(manifest.source_contract,'language_source_v9');
 const record=manifest.records.find(r=>r.source_status!==2);
 assert.equal(record.source_status,0,JSON.stringify(record.source_failure));
 const file=join(out,record.source);execFileSync(process.execPath,['--check',file]);
 const actual=await import(pathToFileURL(file));
 const expected=await import('data:text/javascript;base64,'+Buffer.from(readFileSync(join(fixtures,'entry.js'))).toString('base64'));
 let cases=0;
 function observe(module,name,args){
  const receiver={id:5};const trace=[];
  const call=function(...xs){trace.push([this===receiver,xs.length,...xs.map(x=>typeof x)]);return xs.length;};
  const input=name==='apply'?[call,...args]:args;
  let value=Reflect.apply(module[name],receiver,input);
  if(name==='captured') {const first=value();assert.equal(first,value());value=first;}
  if(name==='changed'){assert.equal(value[0],value[1]);value=value[0];}
  if(['save','captured','changed'].includes(name)){
   assert.equal(Array.isArray(value),false);assert.equal(Object.prototype.toString.call(value),'[object Arguments]');
   assert.throws(()=>value.callee,TypeError);
   value=[value.length,...value];
  }
  return {value,trace};
 }
 for(const args of [[],[undefined],[0],[false],[null],[1,'x',3n],[{},[]]])for(const name of ['save','apply','inspect','captured','changed']){
  assert.deepEqual(observe(actual,name,args),observe(expected,name,args));++cases;
 }
 for(const args of [[],[undefined],[1,2,3]]){
  const a=Reflect.construct(actual.Holder,args),b=Reflect.construct(expected.Holder,args);
  assert.deepEqual([...a.params],[...b.params]);assert.equal(a.params.length,b.params.length);++cases;
 }
 const old=join(root,'previous');execFileSync(engine,[join(fixtures,'entry.abc'),old,'--value-source','--language-numeric','--language-constructors']);
 assert.notEqual(JSON.parse(readFileSync(join(old,'manifest.json'))).records.find(r=>r.source_status!==2).source_status,0);
 recordNodeEvidence({abc:join(fixtures,'entry.abc'),output:out,original:join(fixtures,'entry.js'),producer:import.meta.url,cases,engine,exclusions:['own nonconfigurable throwing caller accessor']});
 // The approved Ark-specific caller descriptor is deliberately not compared.
 console.log(JSON.stringify({published:1,cases,excluded:['own nonconfigurable throwing caller accessor']}));
}finally{rmSync(root,{recursive:true,force:true});}
