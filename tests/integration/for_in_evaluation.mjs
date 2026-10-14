import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure,semantic,abc] = process.argv.slice(2);
const run = (exe,args,status=0) => {
  const r=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
  assert.ifError(r.error);assert.equal(r.signal,null);assert.equal(r.status,status,r.stderr);return r.stdout;
};
const temp=mkdtempSync(join(tmpdir(),'abcd-for-in-evaluation-'));
try {
  for(const [i,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(temp,i);run(structure,[abc,out,'--value-source',...flags]);
    const rows=JSON.parse(readFileSync(join(out,'manifest.json'),'utf8')).records;
    for(const [record,method,pc,causes] of [
      [21089,21280,599,[['FunctionTreeError',0],['EvaluationError',2],['EvaluationObligation',0],['ForInError',8],['TreeObligation',14]]],
      [53454,54264,undefined,[['SyntaxResolutionError',1]]],
    ]) {
      const row=rows.find(r=>r.entity===record);assert.equal(row.source_complete,false);assert.equal(row.source,undefined);
      const f=row.source_failure;
      assert.deepEqual(f.causes,causes.map(([type,value])=>({domain:`abcd::source::${type}`,value})));
      assert.equal(f.method,method);assert.equal(f.pc,pc);assert.equal(f.working_pc,pc);
      const report=JSON.parse(run(semantic,[abc,String(method),'--record-source','--value-source',...flags],1));
      assert.deepEqual(report.source_failure,f);
    }
    // Round 60 discharges Filter's later decision-edge phi transfer; the record
    // now publishes its native loops. Keep strict CLI/manifest source correspondence
    // instead of the obsolete m44496 PC252 DecisionEdgeCopies refusal.
    for(const [entity,method] of [[29268,29450],[43537,44496]]) {
      const complete=rows.find(r=>r.entity===entity);assert.equal(complete.source_complete,true);
      const report=JSON.parse(run(semantic,[abc,String(method),'--record-source','--value-source',...flags]));
      assert.equal(report.source_complete,true);assert.equal(report.source,readFileSync(join(out,complete.source),'utf8'));
      assert.equal(report.source_failure,undefined);
      if(entity===43537)assert.equal((report.source.match(/for \(.* in /g)||[]).length,3);
    }
    // GETITERATOR now has raw semantics, but this for-of method still fails
    // before evaluation at multiple active-region exits. Round 54 proved and
    // projected the inactive acquisition handler; kRegion is no longer the
    // first refusal. Round 55 locates the conflict inside the close-call
    // protection (TryId 0), not the outer loop-body protection.
    const driver=rows.find(r=>r.entity===42397).source_failure;
    assert.equal(driver.method,42717);assert.equal(driver.pc,633);
    assert.equal(driver.working_pc,633);
    const region=driver.exception_region;
    assert.equal(region.region,0);
    assert.equal(region.body_pc,633);assert.equal(region.working_body_pc,633);
    assert.equal(region.handler_pc,658);assert.equal(region.working_handler_pc,658);
    assert.deepEqual(region.exits.map(e=>[e.origin,e.source,e.pc,e.target_pc,e.continuation_pc]),
      [[0,544,656,660,660],[0,534,647,683,683]]);
    assert.deepEqual(driver.causes,[{domain:'abcd::source::PhasedMethodError',value:6},{domain:'abcd::source::ExceptionControlError',value:4}]);
    const blocked=JSON.parse(run(semantic,[abc,'42717','--record-source','--value-source',...flags],1));
    assert.deepEqual(blocked.source_failure,driver);
  }
} finally { rmSync(temp,{recursive:true,force:true}); }
console.log('for-in verified publication and next refusals: ok');
