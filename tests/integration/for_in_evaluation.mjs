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
    // Completion ports discharge m42717; the certified exhaustion continuation
    // also closes m42798. Keep the original bypass/validation edge oracle and
    // require exact complete-source publication through both CLI entry points.
    const instructions=run(semantic,[abc,'42717','--instructions']).trim().split('\n').map(JSON.parse);
    const at=pc=>instructions.find(item=>item.pc===pc);
    for(const pc of [633,658,660,683])assert.ok(at(pc));
    assert.equal(656+at(656).operands[0].bits,660);
    assert.equal(647+at(647).operands[0].bits,683);
    const driver=rows.find(r=>r.entity===42397);
    assert.equal(driver.source_complete,true);assert.equal(driver.source_failure,undefined);
    const source=readFileSync(join(out,driver.source),'utf8');
    run(process.execPath,['--check',join(out,driver.source)]);
    for(const method of [42717,42798]) {
      const complete=JSON.parse(run(semantic,[abc,String(method),'--record-source','--value-source',...flags]));
      assert.equal(complete.source_complete,true);assert.equal(complete.source_failure,undefined);
      assert.equal(complete.source,source);
    }
  }
} finally { rmSync(temp,{recursive:true,force:true}); }
console.log('for-in verified publication and iterator continuation: ok');
