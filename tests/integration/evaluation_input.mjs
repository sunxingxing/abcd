// ABC inputs, never paired source, drive recovery. Staged indexed arrays now
// complete; the unrelated module-contract and decision failures remain checked.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const cases=[
 [42040,42490],
 [41167,41622],
 [20252,20697],
 [37520,38116],
];
// Paths are cohort fixture boundaries, not engine admission criteria. Resolve
// their current configured ABC input rather than guessing an application alias.
const config=JSON.parse(readFileSync(join(root,'tests/corpus/source-cohorts.json')));
const cohort=config.cohorts.find(c=>c.id==='harmony-exploration-2026-10-12');
const ordinals=[8,28,15,24];
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:180000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.signal,null);return p;};
const dir=mkdtempSync(join(tmpdir(),'abcd-evaluation-input-'));
try {
 for(const [ci,[record,method]] of cases.entries()) {
  const abc=join(config.roots[cohort.root],cohort.inputs[ordinals[ci]].path);
  for(const [name,flags] of [['default',[]],['source',['--language-source']]]) {
   const out=join(dir,`${ci}-${name}`),p=run(structure,[abc,out,'--value-source',...flags]);
   assert.equal(p.status,0,p.stderr);
   const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.entity===record);
   const s=run(semantic,[abc,String(method),'--record-source',...flags]);
   const report=JSON.parse(s.stdout);
   if(ci===2&&name==='old') {
    assert.equal(row.source_complete,false);assert.equal(s.status,1,s.stdout);
    assert.deepEqual(row.source_causes,[{domain:'abcd::source::ModuleEnclosureError',value:10}]);
    assert.deepEqual(report.source_causes,row.source_causes);continue;
   }
   if(ci===3) {
    // Since 2026-10-23 a try/finally inside an if is never folded into a
    // short-circuit condition prefix; releaseCamera (m38116) completes.
    assert.equal(s.status,0,s.stdout);assert.equal(row.source_complete,true);assert.equal(report.source_complete,true);
    assert.equal(report.source,readFileSync(join(out,row.source),'utf8'));continue;
   }
   if(ci<3) {
    assert.equal(s.status,0,s.stdout); assert.equal(row.source_complete,true);
    assert.equal(report.source_complete,true); assert.equal(row.source_failure,undefined);
    assert.equal(report.source_failure,undefined);
    assert.match(report.source,/params: \[/);
    continue;
   }
   assert.equal(s.status,1,s.stdout); assert.equal(row.source_complete,false);
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

  }
 }
 console.log('evaluation_input_cli_test: three staged-array completions and releaseCamera, both contracts, retained module refusal and CLI/manifest agreement');
} finally {rmSync(dir,{recursive:true,force:true});}
