// Diagnostic identity regression. No paired source enters the engine.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [semantic,structure,root]=process.argv.slice(2);
const cases=[
 ['UI/CalendarViewSwitch',27642,27869,276,113,4,'complete'],
 ['DocsSample/ArkUISample/DynamicLayout',13863,14290,508,201,17],
 ['DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket',22901,24302,212,93,4,'reference'],
 ['DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket',38531,39900,212,93,4,'reference'],
 ['DocsSample/Media/AVPlayer/AVPlayerArkTSSubtitle',4775,5424,58,28,4,'complete'],
 ['DocsSample/InputMethod/KikaInputMethod',10284,11969,null,null,3,'decision'],
 ['Solutions/InputMethod/KikaInputMethod',10155,11285,null,null,3,'decision'],
 ['DocsSample/ArkTS/NodeAPI/NodeAPIClassicUseCases/NodeAPILoadModuleWithInfo',28894,29074,174,65,17],
 ['DocsSample/ArkTS/NodeAPI/NodeAPIClassicUseCases/NodeAPILoadModuleWithInfo',41871,42137,26,13,4,'complete'],
];
const run=(tool,args)=>{const p=spawnSync(tool,args,{encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});assert.ifError(p.error);assert.equal(p.signal,null);return p;};
const dir=mkdtempSync(join(tmpdir(),'abcd-evaluation-failure-'));
try{
 for(const [app,record,method,pc,operation,obligation,expected='evaluation'] of cases){
  const abc=join(root,app,'artifacts/entry/default/default/modules/modules.abc'),dest=join(dir,String(record));
  const exported=run(structure,[abc,dest,'--value-source','--language-source']);assert.equal(exported.status,0,exported.stderr);
  const row=JSON.parse(readFileSync(join(dest,'manifest.json'))).records.find(r=>r.entity===record);
  const p=run(semantic,[abc,String(method),'--record-source','--language-source']);
  const report=JSON.parse(p.stdout);
  // Preserve exact next-stage expectations. Round 48 already completed the
  // AVPlayer print case (confirmed in explore57/s); Round 59 exposes the two
  // Kika decision-prefix refusals without inventing a PC for their blocks.
  if(expected==='complete') {
   assert.equal(row.source_status,0);assert.equal(p.status,0);assert.equal(report.source_complete,true);
   assert.equal(report.record,record);assert.equal(report.source,readFileSync(join(dest,row.source),'utf8'));
   continue;
  }
  assert.equal(row.source_status,3);assert.equal(p.status,1);
  assert.deepEqual(report.source_failure,row.source_failure);
  const f=report.source_failure;assert.equal(f.method,method);
  if(expected==='decision') {
   assert.deepEqual(f.causes,[{domain:'abcd::source::FunctionTreeError',value:0},{domain:'abcd::source::TreeObligation',value:3}]);
   assert.equal(f.pc,undefined);assert.equal(f.working_pc,undefined);
   assert.deepEqual(f.tree,{obligation:3,location:{method,block:6,control:6}});
   continue;
  }
  assert.equal(f.pc,pc);assert.equal(f.working_pc,pc);
  if(expected==='reference') {
   assert.deepEqual(f.causes,[['FunctionTreeError',1],['ExpressionError',4],['CallPlanError',4],['CallReferenceError',1],['EvaluationKind',1],['TemporaryReason',4]].map(([domain,value])=>({domain:'abcd::source::'+domain,value})));
   assert.deepEqual(f.call_reference,{call:operation,kind:1,placement:1,property:31,temporary:4});
  } else {
   assert.deepEqual(f.causes,[{domain:'abcd::source::EvaluationError',value:5},{domain:'abcd::source::EvaluationObligation',value:obligation}]);
   assert.equal(f.evaluation.location.operation,operation);assert.equal(f.evaluation.location.method,method);
  }
  const instructions=run(semantic,[abc,String(method),'--instructions']);assert.equal(instructions.status,0);
  assert.ok(instructions.stdout.trim().split('\n').map(JSON.parse).some(i=>i.pc===pc));
 }
 console.log('evaluation_failure_cli_test: 2 evaluation refusals, 3 complete records and 4 next-stage refusals agree across publication paths');
}finally{rmSync(dir,{recursive:true,force:true});}
