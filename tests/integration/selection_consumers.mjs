import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-selection-consumers-'));
try {
 const runner=join(dir,'run.mjs');
 writeFileSync(runner,`
 const m=await import(process.argv[2]),mode=process.argv[3];let trace=[],gets=0;
 const boxed={valueOf(){trace.push('valueOf');throw new TypeError();}};
 const x=mode==='null'?null:{get p(){trace.push('get'+(++gets));if(mode==='get'+gets)throw new RangeError();return mode==='falsy'?0:gets===1?boxed:'second';}};
 let rebound=false;
 if(x) for(const key of ['a','b','c','d','e','f'])Object.defineProperty(x,key,{get(){trace.push(key);if(mode===key)throw new RangeError();if(mode==='rebind'&&key==='a')rebound=true;return mode===key+'0'||(key==='b'&&rebound)?0:(key==='d'&&mode==='different'?false:boxed);}});
 const sink={log(v){trace.push(['log',v===boxed?'boxed':v]);if(mode==='log')throw new URIError();},set value(v){trace.push(['set',v===boxed?'boxed':v]);if(mode==='set')throw new SyntaxError();}};
 function call(v){trace.push(['call',v===boxed?'boxed':v]);if(mode==='call')throw new EvalError();return v===boxed?'boxed':v;}
 let results=[];
 for(const f of [()=>m.logical(x,call),()=>m.retained(x,sink),()=>m.booleanJoin(mode==='truthy',boxed,call),()=>m.multi(call,x),()=>m.chain(x,call)]){
  try{let v=f();results.push(v===boxed?'boxed':v);}catch(e){results.push(e.constructor.name);}
 }
 console.log(JSON.stringify({results,trace,gets}));
 `);
 const modes=['ok','null','falsy','truthy','get1','get2','get3','call','log','set','a','b','c','d','e','f','a0','b0','c0','different','rebind'];
 const run=(file,mode)=>spawnSync(process.execPath,[runner,file,mode],{encoding:'utf8',timeout:10000});
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'consumers.js'),oracle);
 const expected=new Map(modes.map(mode=>{const r=run(oracle,mode);assert.equal(r.status,0,r.stderr);assert.ok(!r.stdout.includes('valueOf'));return [mode,r.stdout];}));
 const memberRunner=join(dir,'member-run.mjs'),memberOracle=join(dir,'member-oracle.mjs');
 copyFileSync(join(fixtures,'member.js'),memberOracle);
 writeFileSync(memberRunner,`
 const m=await import(process.argv[2]);const trace=[];
 for(const mode of ['ok','null','a0','b0','c0','a','b','c','replace','callee-throw','noncallable']) {
  const target={};Object.defineProperty(target,'send',{configurable:true,get(){trace.push('callee');if(mode==='callee-throw')throw new RangeError();if(mode==='noncallable')return 0;
   return function(value){trace.push(['send',this===target,value]);return value;};}});
  const input=mode==='null'?null:{};
  if(input)for(const key of ['a','b','c'])Object.defineProperty(input,key,{get(){trace.push(key);if(mode===key)throw new SyntaxError();
   if(mode==='replace')Object.defineProperty(target,'send',{value(){trace.push('wrong-callee');},configurable:true});
   return mode===key+'0'?0:{valueOf(){trace.push('wrong-valueOf');throw new TypeError();}};}});
  try{trace.push(['result',m.argumentsTo(target,input)]);}catch(e){trace.push(['error',e.constructor.name]);}
 }
 console.log(JSON.stringify(trace));`);
 const memberRun=file=>{const r=spawnSync(process.execPath,[memberRunner,file],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);return r.stdout;};
 const memberExpected=memberRun(memberOracle);

 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
  const output=join(dir,label),abc=join(fixtures,'consumers.abc');
  const r=spawnSync(structure,[abc,output,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(r.status,0,r.stderr);
  const records=JSON.parse(readFileSync(join(output,'manifest.json'))).records.filter(r=>r.source_status!==2);assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=join(output,records[0].source),text=readFileSync(file,'utf8');
  const metadata=JSON.parse(readFileSync(join(output,records[0].metadata)));
  const method=metadata.methods.find(m=>m.runtime_name==='retained');assert.ok(method);
  const dis=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(dis.status,0,dis.stderr);
  const ins=dis.stdout.trim().split('\n').map(JSON.parse);assert.ok(ins.filter(i=>i.opcode===0x42).length>=3);assert.ok(ins.some(i=>i.opcode===0x4f||i.opcode===0x51));
  const multiMethod=metadata.methods.find(m=>m.runtime_name==='multi');assert.ok(multiMethod);
  const multiDis=spawnSync(semantic,[abc,String(multiMethod.entity),'--instructions'],{encoding:'utf8'});assert.equal(multiDis.status,0,multiDis.stderr);
  const multiIns=multiDis.stdout.trim().split('\n').map(JSON.parse);
  // Pinned CALLRUNTIME.ISTRUE / ISFALSE plus actual branch/phi-producing
  // bytecode, not only a source spelling that a compiler might optimize away.
  assert.equal(multiIns.filter(i=>i.opcode===0x13fb).length,6);
  assert.equal(multiIns.filter(i=>i.opcode===0x14fb).length,2);
  for(const mode of modes){const actual=run(file,mode);assert.equal(actual.status,0,actual.stderr);assert.equal(actual.stdout,expected.get(mode),label+'/'+mode);}
  const mutations=[text.replaceAll('.p','.wrong'),text.replaceAll('"default"','"wrong"'),text.replaceAll(' && ',' || '),text.replace(/(\$\w+\.p)/g,'($1, $1)')];
  for(const [i,mutant] of mutations.entries()) {assert.notEqual(mutant,text);const file=join(dir,label+'-mutant'+i+'.mjs');writeFileSync(file,mutant);assert.ok(modes.some(mode=>{const actual=run(file,mode);return actual.status!==0||actual.stdout!==expected.get(mode);}));}
  const memberOutput=join(dir,label+'-member');const b=spawnSync(structure,[join(fixtures,'member.abc'),memberOutput,'--value-source',...flags],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);
  const member=JSON.parse(readFileSync(join(memberOutput,'manifest.json'))).records.find(r=>r.source_status!==2);assert.equal(member.source_status,0,JSON.stringify(member));
  const memberFile=join(memberOutput,member.source);assert.equal(memberRun(memberFile),memberExpected);
  const memberText=readFileSync(memberFile,'utf8');assert.match(memberText,/\$arg0\.send\(/);assert.ok(memberText.includes(' && '));
  const wrong=join(dir,label+'-member-wrong.mjs');writeFileSync(wrong,memberText.replaceAll('$arg0.send(','(0, $arg0.send)('));assert.notEqual(memberRun(wrong),memberExpected);
  writeFileSync(wrong,memberText.replace(' && ',' || '));assert.notEqual(memberRun(wrong),memberExpected);
 }
 console.log('selection_consumers_cli_test: 42 original differential executions, 22 new member scenarios, 12 detected mutants; joint member calls complete under both contracts');
}finally{rmSync(dir,{recursive:true,force:true});}
