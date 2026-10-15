import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
const [structure,semantic,probe,abc]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-await-catch-flow-'));
const run=(exe,args,status=0)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.status,status,p.stderr+'\n'+p.stdout);return p.stdout;};
async function trace(file,name,mode){
 const mod=await import(pathToFileURL(file));const log=[];
 const ops={get seed(){log.push('seed:get');return 7;},get otherSeed(){log.push('seed:get');return 8;},wait(){log.push('wait');return mode==='success'?Promise.resolve(5):Promise.reject(new TypeError('input'));},
  get recover(){log.push('recover:get');if(mode==='getter')throw new RangeError('getter');return function(error,preserved,seen){assert.equal(this,ops);assert.equal(seen,log);log.push('recover:call',error.constructor.name,preserved);if(mode==='call')throw new SyntaxError('call');return 11;};},
  get branch(){log.push('branch:get');return mode!=='fallback';},
  get result(){log.push('result:get');if(mode==='result')throw new EvalError('result');return 17;}};
 const promise=mod[name](ops,log);log.push('caller');queueMicrotask(()=>log.push('microtask'));
 try{log.push('resolved',await promise);}catch(error){log.push('rejected',error.constructor.name);}
 await Promise.resolve();return log;
}
try{
 const oracle=join(dir,'oracle.mjs');writeFileSync(oracle,readFileSync(join(dirname(abc),'flow.js')));
 const expected=new Map();
 for(const name of ['checked','ignored','rethrow','cleanup'])for(const mode of ['success','reject','getter','call','fallback','result'])expected.set(name+mode,await trace(oracle,name,mode));
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
  const out=join(dir,label);run(structure,[abc,out,'--value-source',...flags]);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);assert.equal(records.length,1);
  const record=records[0];assert.equal(record.source_status,0,JSON.stringify(record));
  const file=join(out,record.source);run(process.execPath,['--check',file]);
  const meta=JSON.parse(readFileSync(join(out,record.metadata)));
  let queries=0, supplemented=0;
  for(const method of meta.methods.filter(m=>m.runtime_name!=='func_main_0')){
   const instructions=run(semantic,[abc,String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
   assert.ok(instructions.some(i=>i.await_region));
   assert.ok(instructions.some(i=>i.exception_edge?.handler_pc!==undefined));
   const report=JSON.parse(run(probe,[abc,String(method.entity)]));
   if(!report.fast_proof){assert.equal(report.fast_prefix,0);supplemented++;}
   assert.equal(report.catch_flow.verified,true);assert.equal(report.catch_flow.close_projection_authorized,false);
   for(const q of report.catch_flow.queries){assert.ok(q.outcome<4,JSON.stringify(q));assert.equal(q.working_pc,undefined);queries++;}
  }
  assert.ok(queries>=8);assert.ok(supplemented>=1,'fixture must exercise the new proof');
  const source=readFileSync(file,'utf8');
  for(const name of ['checked','ignored','rethrow','cleanup'])for(const mode of ['success','reject','getter','call','fallback','result'])assert.deepEqual(await trace(file,name,mode),expected.get(name+mode),`${label}/${name}/${mode}`);
  // The oracle must notice an erased suspension and a changed preserved local.
  const noAwait=source.replace(/\bawait /g,'');assert.notEqual(noAwait,source);
  const noAwaitFile=join(dir,label+'-no-await.mjs');writeFileSync(noAwaitFile,noAwait);
  assert.notDeepEqual(await trace(noAwaitFile,'checked','success'),expected.get('checkedsuccess'));
  const changed=source.replace(/\.seed\b/,'.otherSeed');assert.notEqual(changed,source);
  const changedFile=join(dir,label+'-changed-local.mjs');writeFileSync(changedFile,changed);
  assert.notDeepEqual(await trace(changedFile,'checked','reject'),expected.get('checkedreject'));
 }
 console.log('await catch flow: raw queries verified; two contracts; 48 engine-output Node traces; suspension/local mutations detected');
}finally{rmSync(dir,{recursive:true,force:true});}
