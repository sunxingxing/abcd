import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-retained-call-'));
try {
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'retained.js'),oracle);
 const runner=join(dir,'runner.mjs');
 writeFileSync(runner,`
 const m=await import(process.argv[2]), rows=[];
 for(const name of ['direct','copied','and','nested'])
 for(const mode of ['truthy','falsy','null','undefined','boxed','callee','earlier','child','later','call','h','rewrite','receiver']) {
  const trace=[];let generation=0;
  const boxed={valueOf(){trace.push('valueOf');throw new Error('ToBoolean cannot call valueOf')}};
  const effect=(name,value)=>{trace.push(name);if(mode===name)throw new RangeError();return value;};
  const o={get call(){effect('callee');const saved=generation;return function(a,b,c){trace.push(['call',this===o,this.mark,saved,a,b===boxed?'boxed':b,c]);if(mode==='call')throw new SyntaxError();return b===boxed?'boxed':b;};},
    get earlier(){return effect('earlier',11)},get later(){return effect('later',23)},mark:0};
  function g(){effect('child');if(mode==='rewrite'){generation++;Object.defineProperty(o,'call',{value(){trace.push('replacement');return -1;}});}
    if(mode==='receiver')o.mark=29;
    return mode==='falsy'?0:mode==='null'?null:mode==='undefined'?undefined:mode==='boxed'?boxed:37;}
  function h(x){return effect('h',x)};
  let result;try{result=m[name](o,g,41,h);}catch(e){result={error:e.constructor.name};}
  rows.push({name,mode,result,trace});
 }
 console.log(JSON.stringify(rows));
 `);
 const run=file=>{const p=spawnSync(process.execPath,[runner,file],{encoding:'utf8',timeout:10000});assert.equal(p.status,0,p.stderr);return JSON.parse(p.stdout);};
 const expected=run(oracle);assert.ok(expected.every(r=>!r.trace.includes('valueOf')));
 let comparisons=0,mutants=0;
 for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
  const out=join(dir,label),abc=join(fixtures,'retained.abc');
  const p=spawnSync(structure,[abc,out,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(p.status,0,p.stderr);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=join(out,records[0].source),text=readFileSync(file,'utf8');
  const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
  for(const name of ['direct','copied','and','nested']) {
   const method=metadata.methods.find(m=>m.runtime_name===name);assert.ok(method);
   const dis=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});assert.equal(dis.status,0,dis.stderr);
   const ops=dis.stdout.trim().split('\n').map(JSON.parse);
   const child=ops.find(i=>i.opcode===0x29),truth=ops.find(i=>i.opcode===0x13fb||i.opcode===0x14fb);
   const branch=ops.find(i=>i.opcode===0x4f||i.opcode===0x51);
   const call=ops.findLast(i=>i.opcode===0x30||i.opcode===0xe0);
   assert.ok(child&&truth&&branch&&call);assert.ok(child.pc<truth.pc&&truth.pc<branch.pc&&branch.pc<call.pc);
   assert.ok(ops.some(i=>i.opcode===0x42&&i.pc<child.pc));
  }
  assert.deepEqual(run(file),expected,label);comparisons+=expected.length;
  const changed=[
   text.replaceAll('$arg1()','($arg1(), $arg1())'),
   text.replaceAll('$arg1()','0'),
   text.replaceAll(' || ',' && '),
   text.replaceAll('$arg0.call(','(0, $arg0.call)('),
   text.replaceAll('$arg0.earlier','($arg0.later, $arg0.earlier)'),
   text.replaceAll('$arg0.call(','($arg0.call, $arg0.call)('),
  ];
  for(const [i,mutant] of changed.entries()) {
   assert.notEqual(mutant,text);const path=join(dir,label+'-mutant'+i+'.mjs');writeFileSync(path,mutant);
   assert.notDeepEqual(run(path),expected,'undetected mutant '+i);++mutants;
  }
  // Two sibling selections remain a separate composition gap. Do not widen
  // the route or the withdrawn joint-consumer admission to make this pass.
  const blocked=join(dir,label+'-twice');const b=spawnSync(structure,[join(fixtures,'twice.abc'),blocked,'--value-source',...flags],{encoding:'utf8',timeout:30000});assert.equal(b.status,0,b.stderr);
  const r=JSON.parse(readFileSync(join(blocked,'manifest.json'))).records.find(r=>r.source_status!==2);assert.equal(r.source_status,3);
  assert.ok(r.source_failure.causes.some(c=>c.domain==='abcd::source::CallReferenceError'&&c.value===1),JSON.stringify(r));
 }
 console.log('retained_call_selection_cli_test:',comparisons,'differentials,',mutants,'detected mutants, 2 typed sibling-selection refusals');
}finally{rmSync(dir,{recursive:true,force:true});}
