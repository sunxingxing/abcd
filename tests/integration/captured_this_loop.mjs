import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
const [engine,fixtures]=process.argv.slice(2);
const registry=JSON.parse(readFileSync(join(fixtures,'build.json')));
for(const [file,hash] of Object.entries(registry.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixtures,file))).digest('hex'),hash,file+' fixture identity');
const dir=mkdtempSync(join(tmpdir(),'abcd-captured-this-loop-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:120000});assert.equal(p.status,0,p.stderr+'\n'+p.stdout);return p.stdout;};
try {
 const oracle=join(dir,'original.mjs');writeFileSync(oracle,readFileSync(join(fixtures,'capture.js')));
 const runner=join(dir,'run.mjs');writeFileSync(runner,`
const m=await import(process.argv[2]);const trace=[];
for(const n of [0,1,3])for(const mode of ['none','break','throw','replace']){
 const out=[],writers=[],receiver={id:7};trace.push([n,mode]);
 globalThis.stop=i=>{trace.push(['stop',i]);if(mode==='throw')throw new TypeError('stop');return mode==='break';};
 globalThis.writer=update=>{trace.push(['writer',typeof update]);writers.push(update);};
 try{trace.push(['result',m.make.call(receiver,n,out)===out]);}catch(e){trace.push(['error',e.constructor.name]);}
 receiver.id=11;if(mode==='replace')for(const writer of writers)writer({id:23});for(const reader of out)trace.push(['read',reader()]);
}
console.log(JSON.stringify(trace));`);
 const expected=run(process.execPath,[runner,oracle]);let comparisons=0;
 for(const [name,flags] of [['default',[]],['source',['--language-source']]]){
  const out=join(dir,name);run(engine,[join(fixtures,'capture.abc'),out,'--value-source',...flags]);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=resolve(out,records[0].source);run(process.execPath,['--check',file]);assert.equal(run(process.execPath,[runner,file]),expected);++comparisons;
  const text=readFileSync(file,'utf8');const bad=text.replace(/return ([$\w]+)\.id;/,'return 99;');assert.notEqual(bad,text,'mutation must change capture');
  const mutated=join(out,'wrong-cell.mjs');writeFileSync(mutated,bad);assert.notEqual(run(process.execPath,[runner,mutated]),expected);
 }
 console.log(JSON.stringify({comparisons,scenarios:24,mutations:2}));
}finally{rmSync(dir,{recursive:true,force:true});}
