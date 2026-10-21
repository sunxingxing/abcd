// Original packaged JS is evaluation-only. Production input is always modules.abc.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {execFileSync} from 'node:child_process';
import {readFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join,dirname} from 'node:path';
import {tmpdir} from 'node:os';
const [engine,root]=process.argv.slice(2);
const config=JSON.parse(readFileSync(join(root,'tests/corpus/source-cohorts.json')));
const cohort=config.cohorts.find(c=>c.id==='harmony-exploration-2026-10-12');
const input=join(config.roots[cohort.root],cohort.inputs[32].path);
const oracle=join(dirname(input),'src/oh_modules/.ohpm/@ohos+hypium@1.0.19/oh_modules/@ohos/hypium/src/main/module/mock');
const temp=mkdtempSync(join(tmpdir(),'abcd-native-argument-records-'));
try{
 const out=join(temp,'out');execFileSync(engine,[input,out,'--value-source','--language-source']);
 const rows=JSON.parse(readFileSync(join(out,'manifest.json'))).records;
 const actual={};for(const [name,id] of [['ExtendInterface',52417],['MockKit',53454]]){
  const r=rows.find(r=>r.entity===id);assert.equal(r.source_status,0,JSON.stringify(r.source_failure));
  const file=join(out,r.source);execFileSync(process.execPath,['--check',file]);actual[name]=readFileSync(file,'utf8');
 }
 async function load(sources){
  const context=vm.createContext({console});
  const extend=new vm.SourceTextModule(sources.ExtendInterface,{context});await extend.link(()=>assert.fail('unexpected dependency'));await extend.evaluate();
  const stub=new vm.SyntheticModule(['default'],function(){this.setExport('default',class{});},{context});await stub.link(()=>{});await stub.evaluate();
  const mock=new vm.SourceTextModule(sources.MockKit,{context});await mock.link(spec=>spec.includes('/ExtendInterface&')?extend:stub);await mock.evaluate();
  return {ExtendInterface:extend.namespace.default,MockKit:mock.namespace.MockKit};
 }
 const modules=[await load({ExtendInterface:readFileSync(join(oracle,'ExtendInterface.js'),'utf8'),MockKit:readFileSync(join(oracle,'MockKit.js'),'utf8')}),await load(actual)];
 let cases=0;
 function observe(module,args,kind){
  const trace=[],receiver={id:7};let snapshot;
  const state={mFunctions:[],getReturnInfo(fn,values){assert.equal(fn,this.mFunctions[0]);snapshot=values;trace.push(['lookup',values.length,...values]);if(kind==='none')return null;if(kind==='mutate'&&values.length)values[0]='changed';return function(...xs){trace.push(['action',this===receiver,xs.length,...xs]);if(kind==='throw')throw new RangeError('action');return xs.length;};},recordMethodCall(method,values){assert.equal(values,snapshot);trace.push(['record',method.name]);},extend(){}};
  const original=function original(){};
  const f=module.MockKit.prototype.mockFunc.call(state,undefined,original);
  let result;try{result=Reflect.apply(f,receiver,args);}catch(e){result=['error',e.name,e.message];}
  assert.equal(Array.isArray(snapshot),false);assert.equal(Object.prototype.toString.call(snapshot),'[object Arguments]');
  assert.throws(()=>snapshot.callee,e=>e.name==='TypeError');
  const first=snapshot;try{Reflect.apply(f,receiver,[]);}catch(e){assert.equal(e.name,'RangeError');}assert.notEqual(first,snapshot);
  const instance=new module.ExtendInterface({});assert.equal(Reflect.apply(instance.stub,instance,args),instance);
  return {result,trace,stored:[instance.params.length,...instance.params]};
 }
 for(const args of [[],[undefined],[0],[null],[false],[1,'x',3n]])for(const kind of ['none','value','throw','mutate']){
  assert.deepEqual(observe(modules[1],args,kind),observe(modules[0],args,kind));++cases;
 }
 console.log(JSON.stringify({published_records:2,differential_cases:cases,excluded:['own caller accessor']}));
}finally{rmSync(temp,{recursive:true,force:true});}
