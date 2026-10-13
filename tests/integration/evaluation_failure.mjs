// Diagnostic identity regression. No paired source enters the engine.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const cases=[
 ['UI/CalendarViewSwitch',27642,27869,276,113,4],
 ['DocsSample/ArkUISample/DynamicLayout',13863,14290,508,201,17],
 ['DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket',22901,24302,116,45,4],
 ['DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket',38531,39900,116,45,4],
 ['DocsSample/Media/AVPlayer/AVPlayerArkTSSubtitle',4775,5424,58,28,4],
 ['Solutions/InputMethod/KikaInputMethod',10155,11832,59,24,16],
 ['DocsSample/ArkTS/NodeAPI/NodeAPIClassicUseCases/NodeAPILoadModuleWithInfo',28894,29074,174,65,17],
 ['DocsSample/ArkTS/NodeAPI/NodeAPIClassicUseCases/NodeAPILoadModuleWithInfo',41871,42137,26,13,4],
];
const run=(tool,args)=>{const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.signal,null);return p;};
const dir=mkdtempSync(join(tmpdir(),'abcd-evaluation-failure-'));
try{
 for(const [app,record,method,pc,operation,obligation] of cases){
  const abc=join(root,app,'artifacts/entry/default/default/modules/modules.abc'),dest=join(dir,String(record));
  const exported=run(structure,[abc,dest,'--value-source','--language-source']);assert.equal(exported.status,0,exported.stderr);
  const row=JSON.parse(readFileSync(join(dest,'manifest.json'))).records.find(r=>r.entity===record);
  assert.equal(row.source_status,3);
  const p=run(semantic,[abc,String(method),'--record-source','--language-source']);assert.equal(p.status,1);
  const report=JSON.parse(p.stdout);assert.deepEqual(report.source_failure,row.source_failure);
  const f=report.source_failure;assert.equal(f.method,method);assert.equal(f.pc,pc);assert.equal(f.working_pc,pc);
  assert.deepEqual(f.causes,[{domain:'abcd::source::EvaluationError',value:5},{domain:'abcd::source::EvaluationObligation',value:obligation}]);
  assert.equal(f.evaluation.location.operation,operation);assert.equal(f.evaluation.location.method,method);
  const instructions=run(semantic,[abc,String(method),'--instructions']);assert.equal(instructions.status,0);
  assert.ok(instructions.stdout.trim().split('\n').map(JSON.parse).some(i=>i.pc===pc));
 }
 console.log('evaluation_failure_cli_test: eight located refusals agree across publication paths');
}finally{rmSync(dir,{recursive:true,force:true});}
