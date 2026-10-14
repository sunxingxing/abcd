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
  ['UI/CalendarViewSwitch', 28303, 28799, 1146],
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
    if(entity===29268) {
      // Increment III now verifies and publishes this native for-in method.
      assert.equal(row.source_complete,true);
      const cli=run(semantic,[abc,String(method),'--record-source','--value-source','--language-source']);
      assert.equal(cli.status,0);const report=JSON.parse(cli.stdout);
      assert.equal(report.record,entity);assert.equal(report.source_complete,true);
      assert.equal(report.source,readFileSync(join(dest,row.source),'utf8'));
      assert.equal(report.source_failure,undefined);
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
    if(entity===28264) {
      const tree=report.source_failure.tree;
      assert.deepEqual(report.source_failure.causes,[
        {domain:'abcd::source::FunctionTreeError',value:0},{domain:'abcd::source::TreeObligation',value:3}]);
      assert.equal(tree.decision_prefix,8); // kPrefixArms: phased event not projected.
      assert.equal(tree.placement,1); // kRoot, not the verified selected value case.
      assert.deepEqual(tree.location,{method:29676,block:94,control:95,operation:47,pc:114,working_pc:111});
      assert.deepEqual(tree.prefix_event,{method:29676,block:94,operation:47,pc:114,working_pc:111});
    }
    if(entity===24892) {
      assert.deepEqual(report.source_causes,[{domain:'abcd::source::RecordModuleKindError',value:1}]);
      assert.equal(report.source_pc,undefined);
    }
    // Round 43 fulfilled the former ExternalRoot requirement for 28303.
    // Round 53 lifted its getiterator and Round 54 projected the inactive
    // region; the first refusal is now the for-of close-protection region
    // (TryId 0, body 1146 / handler 1171) with two distinct continuations.
    if(entity===28303) {
      assert.equal(report.source_failure.working_pc,1146);
      assert.equal(report.source_failure.deferred,undefined);
      assert.deepEqual(report.source_failure.causes,[
        {domain:'abcd::source::PhasedMethodError',value:6},{domain:'abcd::source::ExceptionControlError',value:4}]);
      const region=report.source_failure.exception_region;
      assert.equal(region.region,0);assert.equal(region.body_pc,1146);assert.equal(region.handler_pc,1171);
      assert.deepEqual(region.exits.map(exit=>[exit.pc,exit.target_pc]),[[1169,1173],[1160,1182]]);
    }

  }
  console.log('tree_failure_cli_test: five real refusals and one verified native loop agree across CLI and manifest');
} finally {rmSync(temp,{recursive:true,force:true});}
