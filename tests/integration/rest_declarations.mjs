import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [structure,semantic,fixture,mode]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-rest-declarations-'));
const run=(exe,args,options={})=>{
  const r=spawnSync(exe,args,{encoding:'utf8',timeout:60000,...options});
  assert.equal(r.status,0,r.stderr+'\n'+r.stdout);return r.stdout;
};
const exported=(abc,out)=>{
 run(structure,[abc,out,'--value-source','--language-source']);
 const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
 assert.ok(record);return record;
};
try {
 const manifest=JSON.parse(readFileSync(join(fixture,'manifest.json')));
 for(const [file,hash] of Object.entries(manifest.hashes))
  assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),hash);
 const out=join(dir,'engine'),abc=join(fixture,'rest.abc'),record=exported(abc,out);
 assert.equal(record.source_status,0,JSON.stringify(record));
 const emitted=join(out,record.source);run(process.execPath,['--check',emitted]);
 const original=join(dir,'original.mjs');copyFileSync(join(fixture,'rest.js'),original);
 const runner=join(dir,'run.mjs');writeFileSync(runner,`
import assert from 'node:assert/strict';const m=await import(process.argv[2]);let results=[];
for(const name of ['find','captured','recursive']) {
 assert.equal(m[name].length,1);
 for(const mode of ['normal','value-throw','depth-throw','argument-throw'])
 for(const count of [0,1,3,7]) {
  const trace=[];const obj={get value(){trace.push('value');if(mode==='value-throw')throw new RangeError();return 'value';},get depth(){trace.push('depth');if(mode==='depth-throw')throw new SyntaxError();return 3;}};
  const input={};for(let i=0;i<count;++i)Object.defineProperty(input,i,{get(){trace.push('arg'+i);if(mode==='argument-throw'&&i===0)throw new URIError();return i===1?undefined:i;}});
  const invoke=()=>{const args=[];for(let i=0;i<count;++i)args.push(input[i]);return m[name](obj,...args);};
  let value,error;try{value=invoke();}catch(e){error=e.constructor.name;}
  results.push({name,mode,count,length:m[name].length,value,error,trace});
 }
 const a=m[name]({value:1,depth:1},null,false,0),b=m[name]({value:1,depth:1},null,false,0);
 assert.notEqual(a[1],b[1]);a[1].push('private');assert.deepEqual(b[1],[null,false,0]);
 assert.deepEqual(m[name]({value:1,depth:1})[1],[]);
 assert.deepEqual(m[name]({value:1,depth:1},undefined)[1],[undefined]);
}
console.log(JSON.stringify(results));`);
 const expected=run(process.execPath,[runner,original]);assert.equal(JSON.parse(expected).length,48);
 assert.equal(run(process.execPath,[runner,emitted]),expected);
 const source=readFileSync(emitted,'utf8'),mutation=source.replace('...$arg1','$arg1');
 assert.notEqual(mutation,source);const mutant=join(dir,'mutant.mjs');writeFileSync(mutant,mutation);
 const detected=spawnSync(process.execPath,[runner,mutant],{encoding:'utf8'});
 assert.ok(detected.status!==0||detected.stdout!==expected,'ordinary parameter substituted for rest must be observed');
 // The requested captured-rest example has a distinct, existing raw-scalar
 // staging use. Keep its typed refusal rather than assume that value is dead.
 const negative=exported(join(fixture,'captured_rest.abc'),join(dir,'negative'));
 assert.equal(negative.source_status,3);assert.deepEqual(negative.source_failure.causes.map(c=>[c.domain,c.value]),[
  ['abcd::source::SyntaxResolutionError',1],['abcd::source::RestOwnershipError',2]]);
 assert.ok(negative.source_failure.pc!==undefined);
 // Compare the signature against the pinned compiler, using identical real
 // exports. String export aliases are adapted to identifier aliases only.
 if(mode==='--round-trip') {
 assert.equal(createHash('sha256').update(readFileSync(manifest.compiler)).digest('hex'),manifest.compiler_sha256);
 const recompiled=join(dir,'recompiled.abc'),input=join(dir,'rest.js');
 writeFileSync(input,source.replace(/as "([A-Za-z_$][\w$]*)"/g,'as $1'));
 run(manifest.compiler,[...manifest.options,'--output',recompiled,input]);
 const roundOut=join(dir,'round'),round=exported(recompiled,roundOut);
 const metadata=JSON.parse(readFileSync(join(out,record.metadata))),newMetadata=JSON.parse(readFileSync(join(roundOut,round.metadata)));
 for(const name of ['find','captured','recursive']) {
  const before=metadata.methods.find(m=>m.runtime_name===name),after=newMetadata.methods.find(m=>m.runtime_name===name);
  assert.ok(before&&after);assert.equal(before.abi.parameter_slots,2);assert.equal(after.abi.parameter_slots,2);
  const instructions=(file,method)=>run(semantic,[file,String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse).filter(i=>i.opcode!==undefined);
  // Generated pinned dynamic ISA encoding values, imm8 and prefixed imm16.
  const kCopyRest=207,kWideCopyRest=3069;
  const rest=ops=>ops.filter(i=>i.opcode===kCopyRest||i.opcode===kWideCopyRest).map(i=>i.operands[0].bits);
  assert.deepEqual(rest(instructions(abc,before)),[1]);assert.deepEqual(rest(instructions(recompiled,after)),[1]);
 }
 }
 console.log('48 engine/original rest scenarios; freshness, length 1, helpers, captures, getters/throws and signature mutation; captured-rest raw-scalar refusal preserved'+(mode==='--round-trip'?'; 3 signature round-trips keep COPYRESTARGS(1) / 2 formal slots':''));
} finally {rmSync(dir,{recursive:true,force:true});}
