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
  for(const [i,flags] of [['default',[]],['source',['--language-source']]]) {
    const out=join(temp,i);run(structure,[abc,out,'--value-source',...flags]);
    const rows=JSON.parse(readFileSync(join(out,'manifest.json'),'utf8')).records;
    // Round 60 discharges Filter's later decision-edge phi transfer; the record
    // now publishes its native loops. Keep strict CLI/manifest source correspondence
    // instead of the obsolete m44496 PC252 DecisionEdgeCopies refusal.
    for(const [entity,method] of [[29268,29450],[43537,44496],[21089,21280],...[ [53454,53795],[52417,52655] ]]) {
      const complete=rows.find(r=>r.entity===entity);assert.equal(complete.source_complete,true);
      const report=JSON.parse(run(semantic,[abc,String(method),'--record-source','--value-source',...flags]));
      assert.equal(report.source_complete,true);assert.equal(report.source,readFileSync(join(out,complete.source),'utf8'));
      assert.equal(report.source_failure,undefined);
      if(entity===53454 || entity===52417){assert.match(report.source,/\barguments\b/);run(process.execPath,['--check',join(out,complete.source)]);}
      if(entity===43537)assert.equal((report.source.match(/for \(.* in /g)||[]).length,3);
    }
    // Completion ports discharge m42717's two-continuation close. Keep the
    // original bypass/validation edge oracle independently at the raw CLI,
    // and assert the exact next whole-record failure rather than any failure.
    const instructions=run(semantic,[abc,'42717','--instructions']).trim().split('\n').map(JSON.parse);
    const at=pc=>instructions.find(item=>item.pc===pc);
    for(const pc of [633,658,660,683])assert.ok(at(pc));
    assert.equal(656+at(656).operands[0].bits,660);
    assert.equal(647+at(647).operands[0].bits,683);
    const driver=rows.find(r=>r.entity===42397).source_failure;
    assert.equal(driver.method,42798);
    assert.equal(driver.pc,undefined);assert.equal(driver.working_pc,undefined);
    assert.equal(driver.exception_region,undefined);
    assert.equal(driver.tree.location.method,42798);assert.equal(driver.tree.obligation,10);
    assert.deepEqual(driver.causes,[
      {domain:'abcd::source::FunctionTreeError',value:9},
      {domain:'abcd::source::TreeSemanticError',value:0},
      {domain:'abcd::source::TreeFlowError',value:1},
      {domain:'abcd::source::TreeObligation',value:10},
    ]);
    const blocked=JSON.parse(run(semantic,[abc,'42717','--record-source','--value-source',...flags],1));
    assert.deepEqual(blocked.source_failure,driver);
  }
} finally { rmSync(temp,{recursive:true,force:true}); }
console.log('for-in verified publication and next refusals: ok');
