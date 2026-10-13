import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic, structure, root] = process.argv.slice(2);
const artifact = 'artifacts/entry/default/default/modules/modules.abc';
// Read bytecode only. Paired source never participates in diagnostic identity.
const cases = [
  ['DocsSample/DistributedAppDev/DistributedAuthentication', 4660, 5040, 12],
  ['UI/CustomAnimationTab', 8166, 8556, 13],
  ['DocsSample/ArkTS/ArkTsConcurrent/ApplicationMultithreadingDevelopment/PracticalCasesSecond', 24892, 25024],
  ['DocsSample/Media/Audio/AudioRoutingAndVolumeSample', 12741, 13192, 88],
  ['UI/CalendarViewSwitch', 28303, 28988],
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
    if(entity===24892) {
      assert.deepEqual(report.source_causes,[{domain:'abcd::source::RecordModuleKindError',value:1}]);
      assert.equal(report.source_pc,undefined);
    }
    if(entity===12741) {
      assert.equal(report.source_failure.tree.obligation,6);
      assert.equal(report.source_failure.tree.location.working_pc,85);
      assert.ok(report.source_failure.tree.expected_owner);
      // No unique operation owner exists for this non-root placement.
      assert.equal(report.source_failure.tree.actual_owner,undefined);
    }
    if(entity===28303) {
      const d=report.source_failure.deferred;
      assert.equal(d.reason,0);assert.equal(d.creator,28528);assert.equal(d.child,28988);
      assert.equal(d.allocation.pc,6);assert.equal(d.store.pc,28);assert.equal(d.occurrence.source.pc,22);
    }
    if(entity===29268) assert.deepEqual(report.source_failure.causes,[
      {domain:'abcd::source::PhasedMethodError',value:0},{domain:'abcd::analysis::SemanticError',value:1}]);
  }
  console.log('tree_failure_cli_test: six real refusals agree across CLI and manifest');
} finally {rmSync(temp,{recursive:true,force:true});}
