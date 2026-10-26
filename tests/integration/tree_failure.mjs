import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic, structure, root] = process.argv.slice(2);
const artifact = 'artifacts/entry/default/default/modules/modules.abc';
// Read bytecode only. Paired source never participates in diagnostic identity.
const cases = [
  ['DocsSample/DistributedAppDev/DistributedAuthentication', 4660, 4878, 8],
  ['UI/CustomAnimationTab', 8166, 8638],
  ['DocsSample/ArkTS/ArkTsConcurrent/ApplicationMultithreadingDevelopment/PracticalCasesSecond', 24892, 25024],
  ['UI/CalendarViewSwitch', 28303, 28799, 642],
  ['DocsSample/Media/Audio/AudioSessionSampleJS', 4515, 4696],
  ['DocsSample/DriverDevelopmentKit/ScsiPeripheralDemo', 13920, 14655],
  ['DocsSample/Media/Audio/AudioRoutingAndVolumeSample', 18254, 18564],
  ['UI/CustomAnimationTab', 6952, 7171, 93],
  ['DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket', 28264, 29676, 114],
  ['DocsSample/ArkTS/NodeAPI/NodeAPIClassicUseCases/NodeAPILoadModuleWithInfo', 29268, 29450, 90],
];
const run = (exe,args) => {
  const p=spawnSync(exe,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
  assert.ifError(p.error);assert.equal(p.signal,null);return p;
};
const temp=mkdtempSync(join(tmpdir(),'abcd-tree-failure-'));
try {
  for(const [app,entity,method,pc] of cases) {
    const abc=join(root,app,artifact), dest=join(temp,String(entity));
    const exported=run(structure,[abc,dest,'--value-source','--language-source']);
    assert.equal(exported.status,0,exported.stderr);
    const row=JSON.parse(readFileSync(join(dest,'manifest.json'),'utf8')).records.find(r=>r.entity===entity);
    if(entity===28303 || entity===4515 || entity===29268 || entity===28264 || entity===24892 || entity===6952 || entity===8166 || entity===4660 || entity===13920 || entity===18254) {
      // For-in increment III completes 29268; Round 63 independently verifies
      // the retained phased selection prefix in 28264. Round 74 III/IV completes
      // shared LruCache 24892. All must publish exactly the CLI source.
      assert.equal(row.source_complete,true);
      const cli=run(semantic,[abc,String(method),'--record-source','--value-source','--language-source']);
      assert.equal(cli.status,0);const report=JSON.parse(cli.stdout);
      assert.equal(report.record,entity);assert.equal(report.source_complete,true);
      assert.equal(report.source,readFileSync(join(dest,row.source),'utf8'));
      assert.equal(report.source_failure,undefined);
      if(entity===6952) {
        // Native for-of III publishes only after initial and recovery proofs;
        // the old contract still has the original control refusal.
        assert.match(report.source,/for \([^)]* of /);

      }
      if(entity===24892) {
        assert.equal(row.source_language,'arkts');assert.match(row.source,/\.ets$/);
        assert.match(report.source,/^"use shared";/);

      }

      continue;
    }
    assert.equal(row.source_complete,false);
    const cli=run(semantic,[abc,String(method),'--record-source','--value-source','--language-source']);
    assert.equal(cli.status,1);const report=JSON.parse(cli.stdout);
    assert.equal(report.record,entity);
    for(const key of ['source_failure_domain','source_failure','source_causes','source_tree','source_deferred','source_method','source_pc','source_working_pc'])
      assert.deepEqual(report[key],row[key],`${entity}: ${key}`);
    if(pc!==undefined) {
      assert.equal(report.source_failure.pc,pc);
      assert.equal(report.source_failure.method,method);
      const instructions=run(semantic,[abc,String(method),'--instructions']);
      assert.equal(instructions.status,0);
      // --instructions is streaming JSONL; check the original decoded boundary.
      const lines=instructions.stdout.trim().split('\n').map(line=>JSON.parse(line));
      assert.ok(lines.some(line=>line.pc===pc || line.offset===pc),`${entity}: real instruction boundary ${pc}`);
    }




  }
  console.log('tree_failure_cli_test: every listed record agrees across CLI and manifest');
} finally {rmSync(temp,{recursive:true,force:true});}
