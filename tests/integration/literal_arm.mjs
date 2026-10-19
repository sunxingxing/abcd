import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-literal-arm-'));
const run=(command,args)=>{const p=spawnSync(command,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
try {
 const metadata=JSON.parse(readFileSync(join(fixture,'manifest.json')));
 for(const [file,hash] of Object.entries(metadata.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),hash);
 const output=join(temp,'output');run(engine,[join(fixture,'calls.abc'),output,'--value-source','--language-source']);
 const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status===0);
 assert.ok(record);const emitted=join(output,record.source);run(process.execPath,['--check',emitted]);
 const original=join(temp,'original.mjs');writeFileSync(original,readFileSync(join(fixture,'calls.js')));
 const runner=join(temp,'run.mjs');writeFileSync(runner,`
 const m=await import(process.argv[2]);const log=[];
 for(const name of ['image','fallback','inline','conditional'])
 for(const mode of ['normal','callee-throw','value-throw','replace','branch-throw'])
 for(const value of [undefined,null,false,0,1,'value',{}]) {
   log.push([name,mode,value]);
   const obj={}; const key=name==='image'?'create':'f';
   const replacement=function(v){log.push(['replacement',this===obj,v]);return 'replaced';};
   const original=function(v){log.push(['invoke',this===obj,v,Object.getOwnPropertyDescriptors(v??{})]);return v;};
   Object.defineProperty(obj,key,{configurable:true,get(){log.push('callee');if(mode==='callee-throw')throw new SyntaxError();return original;}});
   const replace=()=>Object.defineProperty(obj,key,{value:replacement,configurable:true});
   const input={get value(){log.push('value');if(mode==='value-throw')throw new RangeError();if(mode==='replace')replace();return value;}};
   const arm=which=>{log.push(which);if(mode==='replace')replace();if(mode==='branch-throw')throw new TypeError();return which;};
   try {const result=name==='conditional'?m[name](obj,!!value,()=>arm('g'),()=>arm('h')):m[name](obj,input);log.push(['return',result]);}
   catch(e){log.push(['throw',e.constructor.name]);}
 }
 console.log(JSON.stringify(log));`);
 assert.equal(run(process.execPath,[runner,emitted]),run(process.execPath,[runner,original]));
 console.log('140 engine-output/original scenarios: values, descriptors, getter/callee order, receiver, replacement, throws');
} finally {rmSync(temp,{recursive:true,force:true});}
