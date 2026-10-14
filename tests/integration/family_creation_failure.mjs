// Evidence only: no class admission or source-body oracle is added by this round.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const inputs=new Map([
 [18,'DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket/artifacts/entry/default/default/modules/modules.abc'],
 [36,'DocsSample/Media/Camera/PhotoSameSource/artifacts/entry/default/default/modules/modules.abc']
].map(([o,path])=>[o,join(root,path)]));
const cases=[
 [18,52728,53236,179,67,53180,2,1154,1161,21],
 [18,59564,60156,232,103,60072,6,1328,1329,32],
 [18,60829,61449,126,48,61365,9,1359,1364,43],
 [18,63861,64425,46,20,64341,2,1437,1443,21],
 [36,19453,20296,18,8,20296,0,250,268,undefined]
];
const dir=mkdtempSync(join(tmpdir(),'abcd-family-creation-'));
const run=(tool,args,status=0)=>{
 const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
 assert.ifError(p.error);assert.equal(p.status,status,p.stderr);return p.stdout;
};
try {
 for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
  const manifests=new Map();
  for(const [o,abc] of inputs) {
   const out=join(dir,`${label}-${o}`);run(structure,[abc,out,'--value-source',...flags]);
   manifests.set(o,JSON.parse(readFileSync(join(out,'manifest.json'))).records);
  }
  for(const [o,record,method,pc,op,family,occurrence,constructor,literal,descendantPc] of cases) {
   const row=manifests.get(o).find(r=>r.entity===record);
   if(o===18 && !(record===63861 && label==='old')){
    // Descendant class preparation now closes the original gate. Keep the real
    // instruction/pool/reachability assertions below as well as source checks.
    assert.equal(row.source_status,0,JSON.stringify(row));
    const cli=JSON.parse(run(semantic,[inputs.get(o),String(method),'--record-source',...flags]));
    assert.equal(cli.source_complete,true);assert.equal(cli.source_failure,undefined);
    assert.equal(cli.source,readFileSync(join(dir,`${label}-${o}`,row.source),'utf8'));
    run(process.execPath,['--check',join(dir,`${label}-${o}`,row.source)]);
   }else if(record===63861 && label==='old'){
    assert.equal(row.source_status,3);
    const f=row.source_failure;assert.equal(f.method,64229);assert.equal(f.pc,62);
    assert.deepEqual(f.causes,[{domain:'abcd::source::CaptureCellError',value:3},{domain:'abcd::source::CaptureError',value:1},{domain:'abcd::source::ScopeError',value:3}]);
    assert.equal(f.capture.slot,7);assert.equal(f.capture.ordinal,2);
    const cli=JSON.parse(run(semantic,[inputs.get(o),String(method),'--record-source',...flags],1));assert.deepEqual(cli.source_failure,f);
   }else{
   assert.equal(row.source_status,3);
   const cli=JSON.parse(run(semantic,[inputs.get(o),String(method),'--record-source',...flags],1));
   assert.deepEqual(cli.source_failure,row.source_failure);
   const failure=cli.source_failure,e=failure.family_creation;
   assert.equal(failure.class,'unproved');assert.equal(failure.method,method);
   assert.equal(failure.pc,pc);assert.equal(failure.working_pc,pc);
   const reason=o===18?14:8; // Verified layout -> preparation gate / unchanged PhasedSite
   const expected=[{domain:'abcd::source::FamilyFileError',value:1},
     {domain:'abcd::source::FamilyCreationCheck',value:reason}];
   if(o===18)assert.equal(e.projected_class,op); // Actual class occurrence, not a function binding.
   assert.deepEqual(failure.causes,expected);
   assert.equal(e.reason,reason);assert.equal(e.root,family);assert.equal(e.occurrence,occurrence);
   assert.equal(e.location.method,method);assert.equal(e.location.operation,op);
   assert.equal(e.location.pc,pc);assert.equal(e.location.working_pc,pc);
   assert.equal(e.kind,2);assert.equal(e.constructor_pool,constructor);assert.equal(e.literal_pool,literal);
   if(descendantPc===undefined)assert.equal(e.descendant_source,undefined);
   else {assert.equal(e.descendant_source.method,family);assert.equal(e.descendant_source.pc,descendantPc);}
   }
   const instructions=run(semantic,[inputs.get(o),String(method),'--instructions']).trim().split('\n').map(JSON.parse);
   const creation=instructions.find(i=>i.pc===pc);
   assert.equal(creation.class_creation.constructor_pool,constructor);
   assert.equal(creation.class_creation.literal_pool,literal);
   // Actual branch targets bypass the preceding returns. The original CFG
   // proof is documented in round-66-family-creations.md; don't erase this class.
   if(method===60156) {
    const at=p=>instructions.find(i=>i.pc===p);
    assert.equal(at(128).opcode,81);assert.equal(128+at(128).operands[0].bits,174);
    assert.equal(at(192).opcode,81);assert.equal(192+at(192).operands[0].bits,229);
    assert.equal(at(229).opcode,112); // LDHOLE; fallthrough to STA then DEFINECLASS
   }
  }
 }
 console.log('family_creation_failure_cli_test: 4 completions + unchanged phased refusal x 2 contracts; CLI/manifest, pool identity and original CFG agree');
} finally {rmSync(dir,{recursive:true,force:true});}
