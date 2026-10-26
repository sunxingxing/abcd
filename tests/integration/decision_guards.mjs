import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure, semantic, fixtures] = process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-decision-guards-'));
try {
  const runner=join(dir,'run.mjs');
  writeFileSync(runner,`
const mode=process.argv[3];globalThis.trace=[];
const m=await import(process.argv[2]);let api;let accesses=0,calls=0;
const limits={get left(){trace.push('left');if(mode==='left')throw new URIError();return 1;},
 get right(){trace.push('right');if(mode==='right')throw new SyntaxError();return 2;}};
api=m.make(limits);
const target={get state(){
 ++accesses;trace.push('state'+accesses);
 if(mode==='getter'+accesses)throw new RangeError();
 if(mode==='rebind'&&accesses===2)api.replace({right:10});
 return {valueOf(){++calls;trace.push('valueOf'+calls);
  if(mode==='valueOf'+calls)throw new TypeError();
  return mode==='firsthit'?1:mode==='secondhit'&&calls===2?2:10;
 }};
}};
const push=trace.push;
trace.push=function(...v){push.apply(this,v);if((mode==='body'&&v[0]==='miss')||(mode==='finally'&&v[0]==='finally'))throw new EvalError();return this.length;};
let result,error;
try{result=api.run(target);}catch(e){error=e.constructor.name;}
console.log(JSON.stringify({result,error,accesses,calls,trace}));
`);
  const run=(file,mode)=>spawnSync(process.execPath,[runner,file,mode],{encoding:'utf8',timeout:10000});
  const modes=['miss','firsthit','secondhit','rebind','getter1','getter2','valueOf1','valueOf2','left','right','body','finally'];
  const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'guards.js'),oracle);
  const expectedFor=mode=>{
    if(mode==='finally') {
      // guards.abc m473: inlined finally PC144-170 throws to user catch
      // H247; its finally PC299-325 throws to catch-all H337. The third
      // finally call at PC377 propagates. Original JS has different ranges.
      return JSON.stringify({error:'EvalError',accesses:2,calls:2,trace:[
        'state1','valueOf1','left','state2','valueOf2','right','miss',
        'finally','EvalError','finally','finally']})+'\n';
    }
    const expected=run(oracle,mode);assert.equal(expected.status,0,expected.stderr);
    return expected.stdout;
  };
  const mismatches=[];
  for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
    const output=join(dir,label),abc=join(fixtures,'guards.abc');
    const exported=spawnSync(structure,[abc,output,'--value-source',...flags],{encoding:'utf8',timeout:30000});
    assert.equal(exported.status,0,exported.stderr);
    const records=JSON.parse(readFileSync(join(output,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const metadata=JSON.parse(readFileSync(join(output,records[0].metadata)));
    const method=metadata.methods.find(m=>m.runtime_name==='run');assert.ok(method);
    const disassembly=spawnSync(semantic,[abc,String(method.entity),'--instructions'],{encoding:'utf8'});
    assert.equal(disassembly.status,0,disassembly.stderr);
    const instructions=disassembly.stdout.trim().split('\n').map(l=>JSON.parse(l));
    // Pinned ISA encodings: LDLEXVAR, named HoleGuard, JEQZ/JNEZ.
    assert.ok(instructions.some(i=>i.opcode===0x3c));
    const guards=instructions.filter(i=>i.opcode===0x09fe);
    assert.ok(guards.length>=2&&guards.every(i=>i.exception_edge?.handler_pc!==undefined));
    assert.equal(instructions.find(i=>i.pc===170)?.exception_edge?.handler_pc,247);
    assert.equal(instructions.find(i=>i.pc===325)?.exception_edge?.handler_pc,337);
    assert.equal(instructions.find(i=>i.pc===377)?.exception_edge?.propagates_to_caller,true);
    const file=join(output,records[0].source),text=readFileSync(file,'utf8');
    assert.equal((text.match(/\.state\.valueOf\(\)/g)||[]).length,2);
    for(const mode of modes){const expected=expectedFor(mode),actual=run(file,mode);assert.equal(actual.status,0,actual.stderr);if(actual.stdout!==expected)mismatches.push({case:label+'/'+mode,expected,actual:actual.stdout});}
    const expression=/\$\w+\.state\.valueOf\(\)/g;
    const mutations=[
      text.replace(expression,s=>`(${s}, ${s})`),
      text.replace(' || ',' && '),
      text.replace(/\$\w+\.right/g,'2'),
    ];
    // An unmutated mismatch cannot count as detecting a mutation.
    const mutationModes=modes.filter(mode=>!mismatches.some(m=>m.case===label+'/'+mode));
    assert.ok(mutationModes.length>0);
    for(const [i,mutant] of mutations.entries()){
      assert.notEqual(mutant,text);const file=join(dir,`${label}-mutant-${i}.mjs`);writeFileSync(file,mutant);
      assert.ok(mutationModes.some(mode=>{const expected=expectedFor(mode),actual=run(file,mode);return actual.status!==0||actual.stdout!==expected;}),'undetected mutation '+i);
    }
  }
  console.log('decision_guards_cli_test: 22 source-oracle and 2 ABC-handler checks; 6 detected mutants');
  assert.deepEqual(mismatches,[], 'source/ABC-handler oracle mismatches');
}finally{rmSync(dir,{recursive:true,force:true});}
