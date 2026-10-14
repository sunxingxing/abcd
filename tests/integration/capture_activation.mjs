import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
const [structure,semantic,fixtures]=process.argv.slice(2);
const run=(exe,args)=>{const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:16*1024*1024});assert.ifError(r.error);assert.equal(r.status,0,r.stderr+'\n'+r.stdout);return r.stdout;};
const dir=mkdtempSync(join(tmpdir(),'abcd-capture-activation-'));
const harness=`
const log=[];let iteration=0,serial=0;
globalThis.__flag=process.argv[3]==='true';
globalThis.__again=()=>++iteration<=3;
globalThis.__probe=o=>{let v;try{v=['read',o.read()];}catch(e){v=['throw',e.constructor.name];}log.push(v);return v;};
globalThis.__init=()=>{log.push(['init',++serial]);if(process.argv[4]==='throw')throw new RangeError('initializer');return serial;};
try{const m=await import(process.argv[2]);log.push(['trace',m.trace]);if(m.readers)log.push(['readers',...m.readers.map(r=>r.read())]);if(m.final)log.push(['final',m.final.read()]);}catch(e){log.push(['import',e.constructor.name]);}
console.log(JSON.stringify(log));
`;
try{
 const runner=join(dir,'runner.mjs');writeFileSync(runner,harness);
 let comparisons=0,mutations=0;
 for(const name of ['branches','variant','loop']){
  const abc=join(fixtures,name+'.abc'),oracle=join(dir,name+'-oracle.mjs');writeFileSync(oracle,readFileSync(join(fixtures,name+'.js')));
  for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]){
   const out=join(dir,name+'-'+label);run(structure,[abc,out,'--value-source',...flags]);
   const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
   assert.equal(records.length,1);const record=records[0];assert.equal(record.source_status,0,JSON.stringify(record));
   const output=join(out,record.source);run(process.execPath,['--check',output]);
   const metadata=JSON.parse(readFileSync(join(out,record.metadata)));
   let classes=0,guards=0,pushes=0;
   // Confirm real es2abc class creation, named HoleGuard, and lexical allocation;
   // numeric encodings here are fixture evidence, never engine admission.
   for(const method of metadata.methods){
    const rows=run(semantic,[abc,String(method.entity),'--instructions']).trim().split('\n').map(JSON.parse);
    classes+=rows.filter(r=>r.class_creation).length;
    guards+=rows.filter(r=>r.opcode===0x09fe).length;
    pushes+=rows.filter(r=>r.opcode===0x09||r.opcode===0xb6).length;
   }
   assert.ok(classes>0 && guards>0 && pushes>0,name+' protocol evidence');
   const evaluate=(p,flag,mode)=>run(process.execPath,[runner,pathToFileURL(resolve(p)).href,flag,mode]);
   for(const flag of ['false','true'])for(const mode of ['value','throw']){
    assert.equal(evaluate(output,flag,mode),evaluate(oracle,flag,mode),name+' '+label+' '+flag+' '+mode);++comparisons;
   }
   // Hoisting to the outer scope destroys both early TDZ and loop activation.
   // Syntax must remain valid; compare exception constructors and all traces,
   // deliberately excluding VM-specific exception messages and stacks.
   const text=readFileSync(output,'utf8');
   const declaration=/let ([$\w]+) = globalThis\.__init\(\);/.exec(text);
   assert.ok(declaration,name+' deferred initialization located');
   const binding=declaration[1];
   const changed='let '+binding+';\n'+text.replace(declaration[0],binding+' = globalThis.__init();');
   const mutant=join(out,'hoisted.mjs');writeFileSync(mutant,changed);run(process.execPath,['--check',mutant]);
   assert.notEqual(evaluate(mutant,'true','value'),evaluate(oracle,'true','value'),name+' TDZ/activation mutation');++mutations;
   if(name==='loop'){
    const read='return '+binding+';';assert.ok(text.includes(read));
    // Preserve the local read (and its early ReferenceError), but return one
    // shared later value. This isolates activation from the TDZ mutation.
    const shared=text.replace(read,binding+'; return globalThis.__shared;')
      .replace(declaration[0],declaration[0]+' globalThis.__shared = '+binding+';');
    const sharedFile=join(out,'shared.mjs');writeFileSync(sharedFile,shared);
    run(process.execPath,['--check',sharedFile]);
    const actual=JSON.parse(evaluate(sharedFile,'true','value'));
    const expected=JSON.parse(evaluate(oracle,'true','value'));
    assert.deepEqual(actual.slice(0,-1),expected.slice(0,-1),'same early TDZ and effect trace');
    assert.notDeepEqual(actual.at(-1),expected.at(-1),'retained readers detect shared activation');++mutations;
   }
  }
 }
 console.log(JSON.stringify({comparisons,mutations}));
}finally{rmSync(dir,{recursive:true,force:true});}
