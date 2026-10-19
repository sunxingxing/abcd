import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {tmpdir} from 'node:os';
const [structure, semantic, fixture]=process.argv.slice(2);
const run=(tool,args)=>{const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.status,0,p.stderr+'\n'+p.stdout);return p.stdout;};
const dir=mkdtempSync(join(tmpdir(),'abcd-descendant-classes-'));
const harness=`
const log=[];
// This global deliberately uses the generated-local spelling: nested class
// free names must be reserved throughout the real creator's enclosure.
globalThis.$value0=(v)=>{log.push(['global',v]);return v;};
Object.defineProperty(Object.prototype,'value',{configurable:true,set(v){log.push(['set',v]);Object.defineProperty(this,'value',{value:v,writable:true,configurable:true});}});
const m=await import(process.argv[2]);
const A=m.factory(),B=m.factory();
log.push(['distinct',A!==B]);log.push(['read',new A(3).read()]);
try{new A(-2);}catch(e){log.push(['throw',e.constructor.name]);}
const holder=new m.Holder();const C=holder.factory(),D=holder.other();log.push(['method',new C(5).read(),new D(6).value,C!==D]);
const all=m.repeated(4);log.push(['loop',all.length,new Set(all).size,...all.map((C,i)=>new C(i).value)]);
log.push(['variant',new (m.variant(true))(7).value,new (m.variant(false))(8).value,m.variant(true)!==m.variant(false)]);
delete Object.prototype.value;delete globalThis.$value0;console.log(JSON.stringify(log));
`;
try{
 const runner=join(dir,'runner.mjs');writeFileSync(runner,harness);
 const oracle=join(dir,'oracle.mjs');writeFileSync(oracle,readFileSync(fixture.replace(/\.abc$/,'.js')));
 const expected=run(process.execPath,[runner,pathToFileURL(oracle).href]);
 for(const [name,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
  const out=join(dir,name);run(structure,[fixture,out,'--value-source',...flags]);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const output=join(out,records[0].source);run(process.execPath,['--check',output]);
  assert.equal(run(process.execPath,[runner,pathToFileURL(resolve(output)).href]),expected);
  const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
  let localClasses=0;
  for(const method of metadata.methods){
   if(!['factory','other','repeated','variant'].includes(method.runtime_name))continue;
   const instructions=run(semantic,[fixture,String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
   localClasses+=instructions.filter(i=>i.class_creation).length;
  }
  assert.equal(localClasses,5,'actual local DEFINECLASS occurrences');
  // These mutations must change observable [[Set]], per-evaluation identity,
  // or constructor exception behavior, not just syntax/check success.
  const text=readFileSync(output,'utf8');
  for(const [kind,changed] of [
   ['set',text.replace(/this\.value = ([^;]+);/g,'Object.defineProperty(this, "value", {value: $1});')],
   ['throw',text.replace(/throw new RangeError\([^;]+;/g,'return;')]]){
   assert.notEqual(changed,text,kind+' mutation located');const path=join(out,kind+'.mjs');writeFileSync(path,changed);
   assert.notEqual(run(process.execPath,[runner,pathToFileURL(path).href]),expected,kind+' mutation detected');
  }
 }
 console.log('descendant_classes_cli_test: both contracts, real bytecode, construction/sets/throw/method/loop identity; mutations detected');
}finally{rmSync(dir,{recursive:true,force:true});}
