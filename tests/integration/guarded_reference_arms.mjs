import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, mkdirSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, dirname} from 'node:path';
const [structure, semantic, fixtures] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-guarded-reference-arms-'));
const run = (exe, args) => spawnSync(exe, args, {encoding:'utf8'});
try {
  let cases=0, mutations=0;
  for(const fixture of ['arms','initialized']) {
  const oracleDir = join(dir,'original-'+fixture); mkdirSync(oracleDir);
  const oracle = join(oracleDir,'arms.mjs');
  writeFileSync(oracle,readFileSync(join(fixtures,fixture+'.js')));
  const dependency = `export const receiver = globalThis.receiver;\n`;
  writeFileSync(join(oracleDir,'dependency.mjs'),dependency);
  const runner = join(dir,'run.mjs');
  writeFileSync(runner, `
const mode=process.argv[3], chosen=process.argv[4]==='yes';
const trace=[]; globalThis.trace=trace;
const receiver={tag:'original'};
Object.defineProperty(receiver,'method',{configurable:true,get(){
  trace.push('callee');if(mode==='callee-throw')throw new RangeError();
  return function(v){trace.push(['call',this?.tag,v]);if(mode==='call-throw')throw new URIError();return v;};
}});
globalThis.receiver=receiver;
let mutation=()=>{};
const flag={get state(){trace.push('condition');
  if(mode==='condition-throw')throw new SyntaxError();
  if(mode==='replace')Object.defineProperty(receiver,'method',{configurable:true,value(){trace.push('replacement');return -1;}});
  if(mode==='receiver')globalThis.receiver={tag:'replacement',method(){trace.push('new receiver');return -2;}};
  if(mode==='write')mutation(43);
  return chosen;
}};
globalThis.left=7;globalThis.right=11;
globalThis.calleeA=function(x){trace.push(['A',this===undefined,x]);return x+1;};
globalThis.calleeB=function(x){trace.push(['B',this===undefined,x]);return x+2;};
globalThis.argument=()=>{trace.push('argument');if(mode==='argument-throw')throw new TypeError();return 5;};
const invoke=(f)=>{try{return ['value',f(flag)];}catch(e){return ['throw',e.constructor.name];}};
let early;
globalThis.probe=(...fns)=>{early=fns.map(invoke);trace.push('initialized-next');};
const m=await import(process.argv[2]); mutation=m.update;
const late=[m.pick,m.fromImport,m.asCallee].map(invoke);
m.update(23); const changed=invoke(m.pick);
console.log(JSON.stringify({early,late,changed,trace}));
`);
  const exec=(file, mode, chosen)=>run(process.execPath,[runner,file,mode,chosen]);
  for (const [label,flags] of [['previous',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(dir,fixture+'-'+label);
    const exported=run(structure,[join(fixtures,fixture+'.abc'),out,'--value-source',...flags]);
    assert.equal(exported.status,0,exported.stderr);
    const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status!==2);
    if(fixture==='arms'&&label==='previous') {
      assert.equal(record?.source_status,3);
      assert.ok(record.source_failure?.causes.some(c=>c.domain==='abcd::source::ScopeError'&&c.value===3));
      continue; // Existing non-SourceTdz contract cannot admit the early captures.
    }
    assert.equal(record?.source_status,0,JSON.stringify(record));
    const file=join(out,record.source), text=readFileSync(file,'utf8');
    writeFileSync(join(dirname(file),'dependency.mjs'),dependency);
    assert.equal(run(process.execPath,['--check',file]).status,0);
    const meta=JSON.parse(readFileSync(join(out,record.metadata)));
    const method=meta.methods.find(m=>m.runtime_name==='pick')??meta.methods.find(m=>m.runtime_name==='');assert.ok(method);
    const raw=run(semantic,[join(fixtures,fixture+'.abc'),String(method.entity),'--instructions']);
    assert.equal(raw.status,0,raw.stderr);
    const instructions=raw.stdout.trim().split('\n').map(JSON.parse).filter(x=>x.pc!==undefined);
    // Generated dynamic ISA: lexical load + named HoleGuard on both arms.
    assert.ok(instructions.filter(i=>i.opcode===0x3c).length>=2);
    assert.ok(instructions.filter(i=>i.opcode===0x09fe).length>=2);
    for(const mode of ['plain','callee-throw','condition-throw','call-throw','argument-throw','replace','receiver','write'])
      for(const chosen of ['yes','no']) {
        const old=exec(oracle,mode,chosen), actual=exec(file,mode,chosen);
        assert.equal(old.status,0,old.stderr);assert.equal(actual.status,0,actual.stderr);
        assert.equal(actual.stdout,old.stdout,label+'/'+mode+'/'+chosen);++cases;
        if(mode==='plain'&&fixture==='arms'){
          const data=JSON.parse(actual.stdout);
          assert.ok(data.early.every(x=>x[0]==='throw'&&x[1]==='ReferenceError'));
          assert.equal(data.trace.filter(x=>x==='argument').length,1);
        }
      }
    const pick=/globalThis\.receiver\.method\(([^;]+)\);/.exec(text);assert.ok(pick);
    const conditional=/([^?]+)\?\s*(\$\w+)\s*:\s*(\$\w+)/.exec(pick[1]);assert.ok(conditional);
    const variants=[
      text.replace(pick[1],pick[1].replace(conditional[2],'void 0')),
      text.replace(pick[1],pick[1].replace(conditional[3],conditional[2])),
      text.replace(pick[1],`(globalThis.receiver.method, ${pick[1]})`),
      text.replace('globalThis.receiver.method(', '(0, globalThis.receiver.method)('),
      text.replace(pick[1],`(($guardSnapshot) => (${pick[1].replaceAll(conditional[2],"$guardSnapshot")}))(${conditional[2]})`),
    ];
    for(const [i,variant] of variants.entries()){
      assert.notEqual(variant,text);
      const mutant=join(dirname(file),`mutant-${i}.mjs`);writeFileSync(mutant,variant);
      let observed=false;
      for(const mode of ['plain','replace','condition-throw','write'])for(const chosen of ['yes','no']){
        const old=exec(oracle,mode,chosen),actual=exec(mutant,mode,chosen);
        observed||=actual.status!==0||actual.stdout!==old.stdout;
      }
      assert.ok(observed,'unobserved mutation '+i);++mutations;
    }
  }
  }
  console.log(JSON.stringify({cases,mutations}));
} finally {rmSync(dir,{recursive:true,force:true});}
