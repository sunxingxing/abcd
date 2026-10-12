// Independent paired-source evidence for the changed loadContent error path.
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import vm from 'node:vm';
import {classReference} from './class_reference.mjs';
import {verifyEntryAbilities} from './entry_ability_lifecycle.mjs';
const [tool,root]=process.argv.slice(2);
const identity=JSON.parse(readFileSync(new URL('../corpus/abc-index.json',import.meta.url))).entries
    .find(row=>row.corpus_id==='paired'&&row.relative_path==='pasteboard/modules.abc');
const bytes=readFileSync(join(root,'modules.abc'));
assert.equal(bytes.length,identity.size);assert.equal(createHash('sha256').update(bytes).digest('hex'),identity.sha256);
const directory=mkdtempSync(join(tmpdir(),'abcd-entryability-load-error-'));
let actual;
try {
  const output=join(directory,'output');
  execFileSync(tool,[join(root,'modules.abc'),output,'--value-source','--language-numeric','--language-constructors'],{encoding:'utf8',timeout:60_000});
  const manifest=JSON.parse(readFileSync(join(output,'manifest.json')));
  const record=manifest.records.find(row=>row.source==='sources/entry/src/main/ets/entryability/EntryAbility.mjs');
  assert.equal(record.source_complete,true);assert.equal(record.legacy_writer_used, undefined);
  assert.equal(manifest.source_contract,'language_numeric_constructors_v1');
  actual=readFileSync(join(output,record.source),'utf8');
} finally {rmSync(directory,{recursive:true,force:true});}
// The paired TypeScript is read only by this independent test, after generation.
const reference=classReference(readFileSync(join(root,'pasteboard_src/src/main/ets/entryability/EntryAbility.ts'),'utf8')).source;
async function observe(source,options) {
  const trace=[],failure=Symbol('failure');let callback;
  const event=(name,...args)=>{trace.push([name,...args]);if(options.fail===name)throw failure;};
  const hilog={info(...args){assert.equal(this,hilog);event('info',...args);},error(...args){assert.equal(this,hilog);event('error',...args);}};
  const context=vm.createContext({AppStorage:{setOrCreate(...args){event('storage',...args);}}});
  const modules={
    '@ohos:app.ability.UIAbility':class {},'@ohos:hilog':hilog,
    '@ohos:display':{},'@ohos:window':{},'@bundle:com.example.pasteboard/entry/ets/common/Logger':{},
  };
  const module=new vm.SourceTextModule(source,{context});
  await module.link(name=>new vm.SyntheticModule(['default'],function(){assert.ok(Object.hasOwn(modules,name));this.setExport('default',modules[name]);},{context}));
  await module.evaluate();
  const json=vm.runInContext('JSON',context);
  json.stringify=function(value){assert.equal(this,json);assert.equal(value,err);event('stringify');return options.result;};
  const err={get code(){event('code');return 1;}};
  const main={getUIContext(){assert.equal(this,main);event('getUIContext');return 'context';}};
  const stage={
    loadContent(path,cb){assert.equal(this,stage);event('loadContent',path);callback=cb;},
    getMainWindowSync(){assert.equal(this,stage);event('getMainWindowSync');return main;},
  };
  let result,thrown;
  try {new module.namespace.default().onWindowStageCreate(stage);result=callback(err,{});}
  catch(error){assert.equal(error,failure);thrown=true;}
  return {trace,result,thrown};
}
let comparisons=0;
for(const result of [undefined,null,'',false,0,'error text'])
  for(const fail of ['none','info','loadContent','getMainWindowSync','getUIContext','storage','code','stringify','error']) {
    const options={result,fail};assert.deepEqual(await observe(actual,options),await observe(reference,options));++comparisons;
  }
console.log(`${comparisons} paired load-error comparisons: receiver, order, nullish fallback and throws`);
// A shared expression transport now also removes the nested breakpoint
// snapshot. Reuse the independent complete-class harness for thresholds,
// coercion/failure ordering, live imports and callbacks; source stays test-only.
console.log(JSON.stringify(await verifyEntryAbilities([{app:'pasteboard',source:actual,reference}])));
