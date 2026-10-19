// Evidence-only regression: ABC inputs, never paired source, drive recovery.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const cases=[
 [42040,42490,2,8,17,47],
 [41167,41622,2,8,17,47],
 [20252,20697,2,8,17,47],
 [37520,38116,null,null,null,null],
];
// Paths are cohort fixture boundaries, not engine admission criteria. Resolve
// their current configured ABC input rather than guessing an application alias.
const config=JSON.parse(readFileSync(join(root,'tests/corpus/source-cohorts.json')));
const cohort=config.cohorts.find(c=>c.id==='harmony-exploration-2026-10-12');
const ordinals=[8,28,15,24];
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.signal,null);return p;};
const dir=mkdtempSync(join(tmpdir(),'abcd-evaluation-input-'));
try {
 for(const [ci,[record,method,leaf,check,operation,pc]] of cases.entries()) {
  const abc=join(config.roots[cohort.root],cohort.inputs[ordinals[ci]].path);
  for(const [name,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
   const out=join(dir,`${ci}-${name}`),p=run(structure,[abc,out,'--value-source',...flags]);
   assert.equal(p.status,0,p.stderr);
   const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.entity===record);
   assert.equal(row.source_complete,false);
   const s=run(semantic,[abc,String(method),'--record-source',...flags]);assert.equal(s.status,1,s.stdout);
   const report=JSON.parse(s.stdout);
   if(ci===2&&name==='old') {
    assert.deepEqual(row.source_causes,[{domain:'abcd::source::ModuleEnclosureError',value:10}]);
    assert.deepEqual(report.source_causes,row.source_causes);continue;
   }
   assert.deepEqual(report.source_failure,row.source_failure);
   const f=row.source_failure;
   if(ci===3) {
    // Activation-local construction now verifies m38333; the record reaches
    // a separate phased decision-prefix obligation in m38116.
    assert.deepEqual(f.causes,[{domain:'abcd::source::FunctionTreeError',value:0},{domain:'abcd::source::TreeObligation',value:3}]);
    assert.equal(f.method,38116);assert.equal(f.pc,undefined);
    assert.deepEqual(f.tree,{obligation:3,decision_prefix:2,
      location:{method:38116,control:1396,synthetic:{kind:2,source:8}},
      prefix_event:{method:38116,control:1396,synthetic:{kind:2,source:8}}});
    continue;
   }
   assert.deepEqual(f.causes,[['EvaluationError',2],['EvaluationObligation',0],['LiteralError',leaf],['EvaluationInputCheck',check]].map(([domain,value])=>({domain:`abcd::source::${domain}`,value})));
   assert.equal(f.method,method);assert.equal(f.evaluation.input_check,check);
   assert.deepEqual(f.evaluation.plan_cause,{domain:'LiteralError',value:leaf});
   assert.equal(f.evaluation.location.operation,operation??undefined);
   assert.equal(f.pc,pc??undefined);assert.equal(f.working_pc,pc??undefined);
  }
 }
 console.log('evaluation_input_cli_test: four records, both contracts, leaf/location and CLI/manifest agreement');
} finally {rmSync(dir,{recursive:true,force:true});}
