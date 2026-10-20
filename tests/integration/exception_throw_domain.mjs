import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
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
 const expected=evaluate(oracle);let published=0,partial=0,refused=0,mutations=0;
 for(const [contract,flags] of [['source',['--language-source']],['previous',['--language-numeric','--language-constructors']]])
 for(const name of ['handler','close','loop','member'])for(const mode of ['O0','default']){
  const out=join(scratch,`${contract}-${name}-${mode}`);
  run(engine,[join(fixtures,name+(mode==='O0'?'':'.default')+'.abc'),out,'--value-source',...flags]);
  const [r]=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  if(mode==='default') {
   assert.equal(r.source_status,3,JSON.stringify(r));
   assert.deepEqual(r.source_failure.causes,[{domain:'abcd::source::PhasedMethodError',value:5},{domain:'abcd::source::ExceptionPlanError',value:2},{domain:'abcd::analysis::CatchRegionError',value:1}]);
   ++refused;continue;
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
  if(name==='close'||name==='loop'){
   assert.equal(r.source_status,3,JSON.stringify(r));
   assert.deepEqual(r.source_failure.causes,[{domain:'abcd::source::PhasedMethodError',value:6},{domain:'abcd::source::ExceptionControlError',value:4}],JSON.stringify(r));
   assert.equal(r.source_failure.exception_region.region,0);++refused;continue;
  }
  assert.equal(r.source_status,0,JSON.stringify(r));++published;
  const output=join(out,r.source);run(process.execPath,['--check',output]);assert.equal(evaluate(output),expected);
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
 console.log(JSON.stringify({published,partial,refused,comparisons:published,behaviorCases:published*5,mutations}));
} finally {rmSync(scratch,{recursive:true,force:true});}
