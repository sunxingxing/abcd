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
  ['UI/CalendarViewSwitch', 28303, 28799, 620],
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
    if(entity===29268 || entity===28264 || entity===24892 || entity===6952) {
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
        const old=run(semantic,[abc,String(method),'--record-source','--value-source','--language-numeric','--language-constructors']);
        assert.equal(old.status,1);
        assert.deepEqual(JSON.parse(old.stdout).source_failure.causes,[
          {domain:'abcd::source::PhasedMethodError',value:6},
          {domain:'abcd::source::ExceptionControlError',value:4}]);
      }
      if(entity===24892) {
        assert.equal(row.source_language,'arkts');assert.match(row.source,/\.ets$/);
        assert.match(report.source,/^"use shared";/);
        const old=run(semantic,[abc,String(method),'--record-source','--language-numeric','--language-constructors']);
        assert.equal(old.status,1);const refused=JSON.parse(old.stdout);
        assert.deepEqual(refused.source_causes,[{domain:'abcd::source::RecordModuleKindError',value:1}]);
        assert.equal(refused.source_pc,undefined);
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
    if(entity===4660 || entity===8166) {
      // Class-self reads are consumed as class-inner bindings; publication
      // stops at the typed recovery gate (ports for 4660, enclosure for 8166).
      assert.deepEqual(report.source_failure.causes,[
        {domain:'abcd::source::ClassSelfError',value:5}]);
      const recovery=report.source_failure.class_self_initialization.recovery;
      assert.equal(recovery.kind,13);
      assert.equal(recovery.projection,entity===4660?1:0);
      assert.equal(recovery.read,5);
      assert.equal(recovery.guard,6);
    }
    if(entity===28303) {
      // Staged activation and for-of cell-lifetime checks now succeed. Keep
      // the exact downstream guarded-reference refusal and original sites.
      assert.equal(report.source_failure.working_pc,620);
      assert.deepEqual(report.source_failure.causes,[
        {domain:'abcd::source::FunctionTreeError',value:1},
        {domain:'abcd::source::ExpressionError',value:4},
        {domain:'abcd::source::CallPlanError',value:4},
        {domain:'abcd::source::CallReferenceError',value:1},
        {domain:'abcd::source::EvaluationKind',value:1},
        {domain:'abcd::source::TemporaryReason',value:4},
        {domain:'abcd::source::GuardedCallError',value:1}]);
      assert.deepEqual(report.source_failure.call_reference,{
        kind:1,call:231,property:183,placement:1,temporary:4,
        checked:{kind:1,call:231,read:20,guard:21,use:257,
          read_pc:56,read_working_pc:56,guard_pc:58,guard_working_pc:58}});
      assert.equal(report.source_failure.tree,undefined);
    }


  }
  console.log('tree_failure_cli_test: three real refusals and four verified complete records agree across CLI and manifest');
} finally {rmSync(temp,{recursive:true,force:true});}
