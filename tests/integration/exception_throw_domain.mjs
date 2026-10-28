import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve, dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [engine, fixtures] = process.argv.slice(2);
const run = (exe,args) => { const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000});assert.ifError(r.error);assert.equal(r.status,0,r.stderr+'\n'+r.stdout);return r.stdout; };
const registry=JSON.parse(readFileSync(join(fixtures,'build.json')));
const hash=p=>createHash('sha256').update(readFileSync(join(fixtures,p))).digest('hex');
for(const f of registry.fixtures){assert.equal(hash(f.source),f.source_sha256);assert.equal(hash(f.abc),f.abc_sha256);}
const scratch=mkdtempSync(join(tmpdir(),'abcd-exception-throw-'));
try {
 const runner=join(scratch,'runner.mjs');
 writeFileSync(runner,`const m=await import(process.argv[2]);const all=[];
for(const mode of ['return','error','getter','primitive','null']){
 const log=[];const failure={get code(){log.push('code');if(mode==='getter')throw new RangeError('getter');return 23;},get message(){log.push('message');return 'bad';}};
 const read=()=>{log.push('read');if(mode==='return')return 7;throw mode==='primitive'?3:mode==='null'?null:failure;};
 try{const value=m.query(read,log);log.push(['result',value!==null && typeof value==='object'?'object':value]);}catch(e){log.push(['throw',e.constructor.name]);}all.push(log);
}console.log(JSON.stringify(all));`);
 const oracle=join(scratch,'original.mjs');writeFileSync(oracle,readFileSync(join(fixtures,'handler.js')));
 const evaluate=p=>run(process.execPath,[runner,pathToFileURL(resolve(p)).href]);
 const expected=evaluate(oracle);let published=0,partial=0,refused=0,mutations=0,loopCases=0,handlerCases=0;
 const loopRunner=join(scratch,'loop-runner.mjs');
 writeFileSync(loopRunner,`import assert from 'node:assert/strict';
const actual=await import(process.argv[2]),original=await import(process.argv[3]);
const fn=m=>m.Tool?new m.Tool().query.bind(new m.Tool()):m.query;
function run(f,mode,outcome){const log=[],error={code:23,message:'failure'};let index=0;
 const item={get stop(){log.push('stop');if(mode==='throw')throw error;return mode==='break';}};
 const xs={[Symbol.iterator](){log.push('acquire');const it={next(){log.push('next');return index++<2?{done:false,value:item}:{done:true};}};
 if(outcome!=='missing')Object.defineProperty(it,'return',{get(){log.push('return-get');if(outcome==='getter')throw error;if(outcome==='null')return null;return function(){log.push(['return',this===it]);if(outcome==='call')throw error;return outcome==='primitive'?1:{};};}});return it;}};
 const seen=[];seen.push=function(x){log.push(x===item?'item':x);return Array.prototype.push.call(this,x);};
 try{log.push(['result',f(xs,seen)]);}catch(e){log.push(['throw',e.constructor.name]);}return log;}
let count=0;for(const mode of ['normal','break','throw'])for(const outcome of ['object','missing','null','getter','call','primitive']){assert.deepEqual(run(fn(actual),mode,outcome),run(fn(original),mode,outcome));++count;}
if(actual.Tool)assert.equal(new actual.Tool().good(),7);console.log(count);`);
 for(const [contract,flags] of [['source',['--language-source']],['default',[]]])
 for(const name of ['handler','close','loop','member'])for(const mode of ['O0','default']){
  const out=join(scratch,`${contract}-${name}-${mode}`);
  run(engine,[join(fixtures,name+(mode==='O0'?'':'.default')+'.abc'),out,'--value-source',...flags]);
  const [r]=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  // Default builds record zero-size handlers (extent inferred from the normal
  // join) and empty protected ranges (inactive). The nested handler build then
  // runs the same oracle as O0; the optimized for-of layouts still enter a
  // protected range by a normal edge and keep that typed refusal.
  if(mode==='default' && name!=='handler') {
   assert.equal(r.source_status,3,JSON.stringify(r));
   assert.deepEqual(r.source_failure.causes,[{domain:'abcd::source::PhasedMethodError',value:5},{domain:'abcd::source::ExceptionPlanError',value:2},{domain:'abcd::analysis::CatchRegionError',value:3}]);
   ++refused;continue;
  }
  if((name==='close'||name==='member'||name==='loop')) {
   assert.equal(r.source_status,0,JSON.stringify(r));++published;
   const output=join(out,r.source);run(process.execPath,['--check',output]);
   if(name==='loop') {
    const metadata=JSON.parse(readFileSync(join(out,r.metadata)));
    const method=metadata.methods.find(m=>m.runtime_name==='query');assert.ok(method);
    assert.match(run(join(dirname(engine),'close_completion_probe'),
      [join(fixtures,'loop.abc'),String(method.entity),'--control']),/protocol-control units=/);
   }
   const reference=join(scratch,name+'-original.mjs');writeFileSync(reference,readFileSync(join(fixtures,name+'.js')));
   loopCases+=Number(run(process.execPath,[loopRunner,pathToFileURL(resolve(output)).href,pathToFileURL(resolve(reference)).href]));
   continue;
  }
  if(name==='member') {
   assert.equal(r.source_status,4,JSON.stringify(r));
   const [omitted]=r.source_omitted_members;
   assert.deepEqual(omitted.causes,[{domain:'abcd::source::PhasedMethodError',value:6},{domain:'abcd::source::ExceptionControlError',value:4}]);
   assert.equal(omitted.exception_region.region,0);
   const output=join(out,r.incomplete_source);run(process.execPath,['--check',output]);
   const check=run(process.execPath,['--input-type=module','-e',`const m=await import(${JSON.stringify(pathToFileURL(resolve(output)).href)});console.log(new m.Tool().good(), typeof new m.Tool().query);`]);
   assert.equal(check.trim(),'7 undefined');++partial;continue;
  }
  assert.equal(r.source_status,0,JSON.stringify(r));++published;
  const output=join(out,r.source);run(process.execPath,['--check',output]);assert.equal(evaluate(output),expected);handlerCases+=5;
  const text=readFileSync(output,'utf8');
  for(const [label,changed] of [
   ['dropped-throw',text.replace(/throw ([$\w]+);/,'return $1;')],
   ['wrong-catch',text.replace(/    try \{\n      return ([^\n]+);\n    \} catch \([^\n]+\) \{\n      [^\n]+\.push\("inner"\);\n      throw [^\n]+;\n    \}/,'    return $1;')],
   ['reordered-read',text.replace(/catch \(([$\w]+)\) \{\n      ([$\w]+)\.push\("inner"\);/,'catch ($1) {\n      $2.push($1.code); $2.push("inner");')],
  ]){
   assert.notEqual(changed,text,label);const bad=join(out,label+'.mjs');writeFileSync(bad,changed);
   run(process.execPath,['--check',bad]);assert.notEqual(evaluate(bad),expected,label);++mutations;
  }
 }
 console.log(JSON.stringify({published,partial,refused,comparisons:published,behaviorCases:handlerCases+loopCases,loopCases,mutations}));
} finally {rmSync(scratch,{recursive:true,force:true});}
