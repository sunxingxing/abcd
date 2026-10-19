import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temporary=mkdtempSync(join(tmpdir(),'abcd-nested-literal-arm-'));
const run=(command,args)=>{const p=spawnSync(command,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
try {
  const manifest=JSON.parse(readFileSync(join(fixture,'manifest.json')));
  for(const [file,hash] of Object.entries(manifest.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),hash);
  const output=join(temporary,'output');run(engine,[join(fixture,'calls.abc'),output,'--value-source','--language-source']);
  const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status===0);
  assert.ok(record);const emitted=join(output,record.source);run(process.execPath,['--check',emitted]);
  const original=join(temporary,'original.mjs');writeFileSync(original,readFileSync(join(fixture,'calls.js')));
  const runner=join(temporary,'run.mjs');writeFileSync(runner,`
const m=await import(process.argv[2]);const trace=[];let count=0;
for(const name of ['nested','middle'])for(const flag of [false,true])
for(const mode of ['normal','callee-throw','noncallable','x-throw','y-throw','before-throw','after-throw','replace','catch-throw','inherited-setter'])
for(const value of [0,null,{identity:7}]) {
  trace.push([name,flag,mode,value]);const receiver={};
  Object.defineProperty(receiver,'showToast',{configurable:true,get(){trace.push('callee');if(mode==='callee-throw')throw new SyntaxError();
    if(mode==='noncallable')return 13;
    return function(argument){trace.push(['invoke',this===receiver,argument,Object.keys(argument),Object.getOwnPropertyDescriptors(argument)]);return argument;};}});
  const input={caught(error){trace.push(['caught',error.constructor.name]);if(mode==='catch-throw')throw new EvalError();return 'caught';}};
  for(const key of ['before','x','y','after'])Object.defineProperty(input,key,{get(){trace.push(key);
    if(mode===key+'-throw' || (mode==='catch-throw' && (key==='x'||key==='y')))throw new RangeError();
    if(mode==='replace')Object.defineProperty(receiver,'showToast',{configurable:true,value(){trace.push('replacement');return 'wrong';}});
    return value;}});
  if(mode==='inherited-setter')for(const key of ['a','b','message','duration'])Object.defineProperty(Object.prototype,key,{configurable:true,set(){trace.push(['setter',key]);throw new Error();}});
  try {trace.push(['result',m[name](receiver,flag,input)]);}catch(error){trace.push(['caller',error.constructor.name]);}
  finally {if(mode==='inherited-setter')for(const key of ['a','b','message','duration'])delete Object.prototype[key];}
  ++count;
}
console.log(JSON.stringify({count,trace}));`);
  const actual=run(process.execPath,[runner,emitted]),expected=run(process.execPath,[runner,original]);
  assert.equal(actual,expected);assert.equal(JSON.parse(actual).count,120);
  console.log('120 nested literal engine-output/original scenarios passed: property order, branches, throws/catch, receiver, callee replacement and descriptors');
} finally {rmSync(temporary,{recursive:true,force:true});}
