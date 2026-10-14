import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,semantic,fixtures]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-cyclic-cells-'));
const run=(tool,args)=>{const p=spawnSync(tool,args,{encoding:'utf8',timeout:30000,maxBuffer:8*1024*1024});assert.ifError(p.error);assert.equal(p.status,0,p.stderr);return p.stdout;};
try {
 const oracle=join(dir,'oracle.mjs');copyFileSync(join(fixtures,'cells.js'),oracle);
 const runner=join(dir,'runner.mjs');writeFileSync(runner,`
 import {pathToFileURL} from 'node:url';
 const m=await import(pathToFileURL(process.argv[2])), results=[];
 for(const name of ['cells','activation'])for(const n of [-1,0,1,2,5])for(const stop of ['none','init1','init2','write1','write2']) {
  const trace=[],count={init:0,write:0};let result;
  const effect=(kind,value)=>{trace.push([kind,value]);if(kind+(++count[kind])===stop)throw new ReferenceError(kind);return value;};
  try{result=m[name](n,effect);}catch(e){result={error:e.constructor.name};}
  results.push({name,n,stop,result,trace});
 }
 console.log(JSON.stringify(results));
 `);
 const observed=file=>JSON.parse(run(process.execPath,[runner,file]));
 const expected=observed(oracle);let comparisons=0,mutants=0;
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
  const out=join(dir,label),abc=join(fixtures,'cells.abc');
  run(structure,[abc,out,'--value-source',...flags]);
  const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
  assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
  const file=join(out,records[0].source),text=readFileSync(file,'utf8');
  const metadata=JSON.parse(readFileSync(join(out,records[0].metadata)));
  const method=metadata.methods.find(m=>m.runtime_name==='cells');assert.ok(method);
  const instructions=run(semantic,[abc,String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
  assert.ok(instructions.some(i=>i.opcode===9||i.opcode===0x6a)); // fresh lexical environment
  assert.ok(instructions.some(i=>i.opcode===0x3c));assert.ok(instructions.some(i=>i.opcode===0x3d));
  assert.ok(instructions.some(i=>i.opcode===0x33)); // captures, rather than an uncaptured register-only loop
  run(process.execPath,['--check',file]);assert.deepEqual(observed(file),expected);comparisons+=expected.length;
  for(const [i,mutant] of [text.replaceAll(' + 1',' + 2'),text.replaceAll('"init", 3','"init", 9'),
      text.replaceAll('"write"','"init"')].entries()) {
   assert.notEqual(mutant,text);const path=join(dir,`${label}-mutant${i}.mjs`);writeFileSync(path,mutant);
   assert.notDeepEqual(observed(path),expected);++mutants;
  }
 }
 console.log('cyclic_cells_cli_test:',comparisons,'Node comparisons;',mutants,'detected mutations');
}finally{rmSync(dir,{recursive:true,force:true});}
