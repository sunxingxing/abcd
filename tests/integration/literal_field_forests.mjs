import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
const [engine,fixtures]=process.argv.slice(2);
const registry=JSON.parse(readFileSync(join(fixtures,'build.json')));
for(const [file,hash] of Object.entries(registry.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixtures,file))).digest('hex'),hash,file+' fixture identity');
const dir=mkdtempSync(join(tmpdir(),'abcd-literal-field-forests-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.equal(p.status,0,p.stderr+'\n'+p.stdout);return p.stdout;};
try {
 const oracle=join(dir,'original.mjs');writeFileSync(oracle,readFileSync(join(fixtures,'fields.js')));
 const runner=join(dir,'run.mjs');writeFileSync(runner,`
const trace=[];globalThis.trace=trace;let mode,receiver;
globalThis.utility={get pick(){trace.push('pick-get');if(mode==='pick-get-throw')throw new EvalError();return function(v){trace.push(['pick',this===globalThis.utility,v]);return v;};}};
globalThis.Constructor=class {constructor(...args){trace.push(['construct',args]);if(mode==='constructor-throw')throw new URIError();this.args=args;}};
globalThis.field=name=>{trace.push(['field',name]);if(mode===name+'-throw')throw new RangeError();if(mode==='replace')Object.defineProperty(receiver,'method',{configurable:true,value(){trace.push('wrong-replacement');}});return name;};
const m=await import(process.argv[2]);
let count=0;
for(const name of (process.argv[3]==='loop'?['loop']:['run','conditional']))for(const left of [false,true])for(const right of [false,true])for(const last of [false,true])
for(mode of ['normal','callee-throw','noncallable','first-throw','pick-throw','pick-get-throw','constructor-throw','a-throw','b-throw','replace','setter']){
 receiver={};trace.push(['scenario',name,left,right,last,mode]);
 Object.defineProperty(receiver,'method',{configurable:true,get(){trace.push('callee');if(mode==='callee-throw')throw new SyntaxError();if(mode==='noncallable')return 17;return function(arg){trace.push(['invoke',this===receiver,arg,Object.keys(arg),Object.getOwnPropertyDescriptors(arg)]);return arg;};}});
 const state={};for(const [key,value] of Object.entries({left,right,last,a:11,b:22,c:33,d:44}))Object.defineProperty(state,key,{get(){trace.push(['state',key]);if(mode===key+'-throw')throw new TypeError();return value;}});
 if(mode==='setter')for(const key of ['first','guarded','made','selected'])Object.defineProperty(Object.prototype,key,{configurable:true,set(){trace.push(['inherited-setter',key]);throw new Error();}});
 try{trace.push(['result',m[name](receiver,name==='loop'?{[Symbol.iterator](){let i=0;return{next(){trace.push(['next',i]);return i++<2?{done:false,value:state}:{done:true};},return(){trace.push('close');return{done:true};}};}}:state)]);}catch(e){trace.push(['caller',e.constructor.name]);}
 finally{if(mode==='setter')for(const key of ['first','guarded','made','selected'])delete Object.prototype[key];}
 ++count;
}
console.log(JSON.stringify({count,trace}));`);
 const expected=run(process.execPath,[runner,oracle]);assert.equal(JSON.parse(expected).count,176);
 let comparisons=0,mutations=0;
 for(const [name,flags] of [['previous',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
  const out=join(dir,name);run(engine,[join(fixtures,'fields.abc'),out,'--value-source',...flags]);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=resolve(out,records[0].source);run(process.execPath,['--check',file]);assert.equal(run(process.execPath,[runner,file]),expected);++comparisons;
  const text=readFileSync(file,'utf8');
  for(const [label,bad] of [['wrong-field',text.replaceAll('first:','wrong:').replaceAll('before:','wrong:')],['wrong-callee',text.replaceAll('.method(','.missing(')],['wrong-arm',text.replaceAll('.a', '.b')]]){
   assert.notEqual(bad,text,label+' must mutate');const mutated=join(out,label+'.mjs');writeFileSync(mutated,bad);run(process.execPath,['--check',mutated]);assert.notEqual(run(process.execPath,[runner,mutated]),expected,label);++mutations;
  }
 }
 const loopOracle=join(dir,'loop-original.mjs');writeFileSync(loopOracle,readFileSync(join(fixtures,'loop.js')));
 const loopOut=join(dir,'loop');run(engine,[join(fixtures,'loop.abc'),loopOut,'--value-source','--language-source']);
 const loopRecord=JSON.parse(readFileSync(join(loopOut,'manifest.json'))).records.find(r=>r.source_status!==2);
 assert.equal(loopRecord.source_status,0,JSON.stringify(loopRecord));const loopFile=resolve(loopOut,loopRecord.source);run(process.execPath,['--check',loopFile]);
 assert.equal(run(process.execPath,[runner,loopFile,'loop']),run(process.execPath,[runner,loopOracle,'loop']));++comparisons;
 const negative=join(dir,'tdz-refusal');run(engine,[join(fixtures,'tdz.abc'),negative,'--value-source','--language-source']);
 const refused=JSON.parse(readFileSync(join(negative,'manifest.json'))).records.find(r=>r.source_status!==2);
 assert.equal(refused.source_status,3);assert.deepEqual(refused.source_failure.causes,[{domain:'abcd::source::FunctionTreeError',value:2},{domain:'abcd::source::TreeObligation',value:6}]);
 console.log(JSON.stringify({comparisons,scenarios:440,mutations,typed_refusals:1}));
}finally{rmSync(dir,{recursive:true,force:true});}
