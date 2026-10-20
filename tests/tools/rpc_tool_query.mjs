// Evaluation only: neither paired source nor extracted method text enters ABCD.
import assert from 'node:assert/strict';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import {join} from 'node:path';
const [emittedFile,sourceFile,out]=process.argv.slice(2);
const require=createRequire(import.meta.url);
const acorn=require('internal/deps/acorn/acorn/dist/acorn');
const tsPath='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js';
const ts=require(tsPath);
const source=readFileSync(sourceFile,'utf8'),emitted=readFileSync(emittedFile,'utf8');
const ast=ts.createSourceFile(sourceFile,source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
const originals=[];
function visit(n){if(ts.isMethodDeclaration(n)&&n.name?.getText(ast)==='query')originals.push(n.getText(ast));ts.forEachChild(n,visit);}
visit(ast);assert.equal(originals.length,1);
const printed=[];
function walk(n){if(!n||typeof n!=='object')return;if(n.type==='MethodDefinition'&&n.key?.name==='query')printed.push(emitted.slice(n.start,n.end));for(const value of Object.values(n)){if(Array.isArray(value))value.forEach(walk);else if(value&&typeof value==='object')walk(value);}}
walk(acorn.parse(emitted,{ecmaVersion:'latest',sourceType:'module'}));assert.equal(printed.length,1);
const originalJs=ts.transpileModule('class Subject {\n'+originals[0]+'\n}',{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText;
const engineJs='class Subject {\n'+printed[0]+'\n}';
function run(text,mode,outcome,bodyFailure,early) {
  const log=[],body=new RangeError('body'),close=new URIError('close');body.code=11;close.code=12;
  const devices=[1,2].map((id)=>{
    const device={toString(){log.push(['string',id]);return 'device'+id;}};
    for(const [key,value] of Object.entries({deviceId:id,vendorId:3,productId:mode==='match'?10:99,busType:4,description:'desc'}))
      Object.defineProperty(device,key,{get(){log.push(['read',id,key]);if(bodyFailure===key)throw body;return value;}});
    return device;
  });
  const iterable={[Symbol.iterator](){log.push('acquire');let i=0;
    const iterator={get next(){log.push('next-get');return function(){log.push(['next',this===iterator]);if(mode==='next-throw')throw body;return i<devices.length?{done:false,value:devices[i++]}:{done:true};}}};
    if(outcome!=='missing')Object.defineProperty(iterator,'return',{get(){log.push('return-get');if(outcome==='getter-throw')throw close;if(outcome==='null')return null;return function(){log.push(['return',this===iterator]);if(outcome==='call-throw')throw close;return outcome==='primitive'?1:{};};}});
    return iterator;
  }};
  const context={hilog:{info(...a){log.push(['info',...a]);},error(...a){log.push(['error',...a]);}},deviceManager:{BusType:{USB:7},queryDevices(x){log.push(['query',x]);if(mode==='query-throw')throw body;return mode==='empty'?[]:iterable;}},PRODUCT_ID_LIST:[10,20]};
  const fn=new vm.Script(text+'\nSubject.prototype.query;').runInNewContext(context);
  const receiver={isBinding:early==='binding',bindState:false,deviceId:early==='known'?3:-1,isQuerying:false};
  let stored;Object.defineProperty(receiver,'devices',{get(){log.push('devices-get');if(mode==='receiver-get-throw')throw body;return stored;},set(v){log.push('devices-set');stored=v;}});
  try{log.push(['result',fn.call(receiver)]);}catch(e){log.push(['escaped',e.constructor.name,e===body,e===close]);}
  log.push(['state',receiver.deviceId,receiver.vendorId,receiver.productId,receiver.description,receiver.message,receiver.isQuerying]);
  return log;
}
let cases=0;
for(const mode of ['match','none','empty','query-throw','next-throw','receiver-get-throw'])
  for(const outcome of ['object','missing','null','getter-throw','call-throw','primitive'])
    for(const failure of [null,'productId','description'])for(const early of [null,'binding','known']) {
      assert.deepEqual(run(engineJs,mode,outcome,failure,early),run(originalJs,mode,outcome,failure,early),`${mode}/${outcome}/${failure}/${early}`);++cases;
    }
mkdirSync(out,{recursive:true});
writeFileSync(join(out,'engine-query.js'),engineJs+'\n');writeFileSync(join(out,'original-query.js'),originalJs+'\n');
writeFileSync(join(out,'comparison.md'),'# query() evaluation\n\nEngine output:\n```js\n'+printed[0]+'\n```\n\nOriginal evaluation-only source:\n```ts\n'+originals[0]+'\n```\n');
const report={emitted:emittedFile,original:sourceFile,cases,typescript:{path:tsPath,sha256:createHash('sha256').update(readFileSync(tsPath)).digest('hex')}};
writeFileSync(join(out,'node-report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
