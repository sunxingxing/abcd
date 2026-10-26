import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {join, dirname} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
const [structure, semantic, abc] = process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-cell-parameter-defaults-'));
const run=(exe,args,status=0)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:30000});assert.equal(p.status,status,p.stderr+'\n'+p.stdout);return p.stdout;};
const instructions=(file,method)=>run(semantic,[file,String(method),'--instructions']).trim().split('\n').map(JSON.parse).filter(x=>x.opcode!==undefined);
const compile='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';
function exported(file,out,flags) {
  run(structure,[file,out,'--value-source',...flags]);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);const r=records[0];assert.equal(r.source_status,0,JSON.stringify(r));
  const path=join(out,r.source);run(process.execPath,['--check',path]);
  return {path,source:readFileSync(path,'utf8'),meta:JSON.parse(readFileSync(join(out,r.metadata)))};
}
async function trace(path) {
  const mod=await import(pathToFileURL(path));const log=[];const a=mod.make(7,log);
  assert.equal(a.f.length,2);const first=a.f(1,2);assert.equal(first.get(),7);
  a.set(8);const second=a.f(1,2,undefined);assert.equal(second.get(),8);first.set(9);
  assert.equal(first.get(),9);assert.equal(second.get(),8);
  for(const value of [null,false,0])assert.equal(a.f(1,2,value).get(),value);
  const before=[];assert.throws(()=>mod.early(before,false),ReferenceError);assert.deepEqual(before,[]);
  assert.equal(mod.early(before,true)(),3);assert.deepEqual(before,['body']);
  return [log,before,first.get(),second.get()];
}
try {
  const fixture=dirname(abc);const oracle=join(dir,'oracle.mjs');writeFileSync(oracle,readFileSync(join(fixture,'behavior.js')));
  const expected=await trace(oracle);
  for(const [label,flags] of [['default',[]],['source',['--language-source']]]) {
    const out=exported(abc,join(dir,label),flags);
    const f=out.meta.methods.find(m=>m.runtime_name==='f');assert.ok(f);
    const ins=instructions(abc,f.entity);
    assert.ok(ins.some(i=>i.opcode===0x3d&&i.operands[0].bits===0));
    assert.ok(ins.some(i=>i.opcode===0x3c&&i.operands[0].bits===1));
    assert.ok(ins.some(i=>i.opcode===0x09fe&&i.exception_edge.propagates_to_caller));
    const report=JSON.parse(run(semantic,[abc,String(f.entity),'--record-source',...flags]));
    assert.equal(report.source_complete,true);assert.equal(report.source,out.source);
    const mod=await import(pathToFileURL(out.path));const fn=mod.make(11);assert.equal(fn.length,2);
    assert.equal(fn(0,0)(),11);assert.equal(fn(0,0,undefined)(),11);assert.equal(fn(0,0,0)(),0);
    const behavior=exported(join(fixture,'behavior.abc'),join(dir,label+'-behavior'),flags);
    assert.deepEqual(await trace(behavior.path),expected);
    const erasedGuard=behavior.source.replace(/(\$arg\d+) = (\$local\d+)/g,'$1 = undefined');
    assert.notEqual(erasedGuard,behavior.source);
    const erasedPath=join(dir,label+'-erased-guard.mjs');writeFileSync(erasedPath,erasedGuard);
    await assert.rejects(()=>trace(erasedPath));
    // Observable mutation oracles: early declaration destroys TDZ, and reading
    // the outer default unconditionally breaks the provided-argument path.
    const early=readFileSync(oracle,'utf8');
    for(const [name,text] of [['early',early.replace('let outer = 9;', '') .replace('export function early(log, provided) {','export function early(log, provided) { let outer = 9;')],
      ['provided',early.replace('let result = provided ?', 'outer; let result = provided ?')]]) {
      const file=join(dir,label+'-'+name+'.mjs');writeFileSync(file,text);await assert.rejects(()=>trace(file));
    }
    // Recompile actual engine output. Names/slots may differ; compare storage
    // roles and semantic commitments, never raw offsets or pool ordinals.
    const js=join(dir,label+'-roundtrip.js'),compiled=join(dir,label+'-roundtrip.abc');// Pinned es2abc predates string export names; only identifier-valued export
    // aliases are unquoted for this compiler boundary. Function text is exact.
    writeFileSync(js,out.source.replace(/ as "([A-Za-z_$][\w$]*)"/g,' as $1'));
    run(compile,['--module','--merge-abc','--opt-level=0','--output='+compiled,js]);
    const roundDir=join(dir,label+'-roundtrip');run(structure,[compiled,roundDir]);
    const metadata=JSON.parse(readFileSync(join(roundDir,'manifest.json'))).records.map(r=>JSON.parse(readFileSync(join(roundDir,r.metadata))));
    const methods=metadata.find(r=>r.module!==null&&r.module!==undefined).methods;const parameter=methods.find(m=>m.abi?.parameter_slots===3);assert.ok(parameter);
    const ops=instructions(compiled,parameter.entity);
    assert.ok(ops.some(i=>i.opcode===0x3c&&i.operands[0].bits===1),'separate outer cell');
    assert.ok(ops.some(i=>i.opcode===0x3d&&i.operands[0].bits===0),'body cell initialization');
    assert.ok(ops.some(i=>i.opcode===0x09fe&&i.exception_edge.propagates_to_caller),'TDZ guard');
    const decision=ops.find(i=>i.opcode===0x4f),read=ops.find(i=>i.opcode===0x3c&&i.operands[0].bits===1);
    const guard=ops.find(i=>i.opcode===0x09fe),store=ops.find(i=>i.opcode===0x3d&&i.operands[0].bits===0);
    assert.ok(decision&&read&&guard&&store);
    const joinPc=decision.pc+decision.operands[0].bits;
    assert.ok(decision.pc<read.pc&&read.pc<guard.pc&&guard.pc<joinPc&&joinPc<=store.pc,'provided edge bypasses read/guard before body commit');
    const closures=methods.filter(m=>m.abi?.parameter_slots===0&&m.runtime_name!=='func_main_0');
    assert.ok(closures.some(m=>instructions(compiled,m.entity).some(i=>i.opcode===0x3c&&i.operands[0].bits===0&&i.operands[1].bits===store.operands[1].bits)),'closure targets body cell');
    const creations=methods.flatMap(m=>instructions(compiled,m.entity)).filter(i=>i.opcode===0x33&&i.operands.some(o=>o.method_entity===parameter.entity));
    assert.equal(creations.length,1);assert.equal(creations[0].operands.at(-1).bits,2,'recompiled native length');
  }
  console.log('cell_parameter_defaults_cli_test: both contracts, Node behavior, TDZ mutations, engine-output recompile/ABI/cell/guard round-trip');
} finally {rmSync(dir,{recursive:true,force:true});}
