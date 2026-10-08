import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {readFileSync, mkdirSync, mkdtempSync} from 'node:fs';
import {join} from 'node:path';
import {stripTypeScriptTypes} from 'node:module';
import vm from 'node:vm';
import {parseJavaScript} from './class_reference.mjs';
import {editExpression} from './expression_edits.mjs';
const [tool,abc,paired,outputRoot]=process.argv.slice(2);
mkdirSync(outputRoot,{recursive:true});const directory=join(mkdtempSync(join(outputRoot,'callback-')),'export');
// Complete ABC-only generation before opening the independent paired source.
const run=spawnSync(tool,[abc,directory,'--value-source','--language-numeric','--language-constructors'],{encoding:'utf8',timeout:60_000});
assert.equal(run.status,0,run.stderr);
const manifest=JSON.parse(readFileSync(join(directory,'manifest.json'),'utf8'));
const record=manifest.records.find(value=>value.source?.endsWith('/pages/PasteBoard.mjs'));
assert.ok(record);assert.equal(record.source_complete,true);assert.equal(record.source_status,0);
const recovered=readFileSync(join(directory,record.source),'utf8');
const reference=stripTypeScriptTypes(readFileSync(paired,'utf8'));
function callback(source){
  const pending=[parseJavaScript(source)],matches=[];
  while(pending.length){const node=pending.pop();
    if(node.type==='CallExpression'&&node.callee.type==='MemberExpression'&&node.callee.object.name==='PasteButton'&&node.callee.property.name==='onClick')matches.push(node.arguments[0]);
    for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
  assert.equal(matches.length,1);assert.equal(matches[0].type,'ArrowFunctionExpression');assert.equal(matches[0].async,true);
  return source.slice(matches[0].start,matches[0].end);
}
const actual=callback(recovered),expected=callback(reference);
const declarations=[];const pending=[parseJavaScript('('+actual+')')];
while(pending.length){const node=pending.pop();if(node.type==='VariableDeclaration'&&node.kind==='let')declarations.push(...node.declarations);
  for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
assert.equal(declarations.length,3); // one success flag and two loop indices
const mime={H:'text/html',T:'text/plain',P:'image/pixel',X:'unsupported'};
async function observe(source,pairedSource,types,result,mode,failAt=0){
  const trace=[];let steps=0;const failure=Symbol('original failure');
  const event=(...parts)=>{trace.push(parts);if(++steps===failAt)throw failure;};
  const valueKind=value=>typeof value==='object'&&value!==null?Object.prototype.toString.call(value):value;
  class StyledString {constructor(value){event('styled',valueKind(value));this.value=value;}}
  const records=types.map((sequence,index)=>{
    const values=sequence.map(type=>mime[type]);
    const proxy=new Proxy(values,{get(target,key,receiver){
      if(key==='length'){event('types-length',index);return mode==='length-coercion'?{[Symbol.toPrimitive](hint){event('length-coerce',index,hint);return target.length;}}:target.length;}
      if(typeof key==='string'&&/^\d+$/.test(key))event('type',index,Number(key));
      return Reflect.get(target,key,receiver);}});
    const record={getValidTypes(target){assert.equal(this,record);event('valid-types',index,...Array.from(target));return proxy;}};
    Object.defineProperty(record,'getData',{get(){event('read-data-call',index);return function(type){assert.equal(this,record);event('record-data',index,type);
      if(mode==='record-reject')return Promise.reject(failure);
      return Promise.resolve(index+':'+type).then(value=>{event('record-resume',index,type);return value;});};}});
    return record;
  });
  const data={getRecordCount(){assert.equal(this,data);event('record-count');return records.length;},
    getRecord(index){assert.equal(this,data);event('record',index);return records[index];}};
  const system={getData(){assert.equal(this,system);event('system-data');if(mode==='system-reject')return Promise.reject(failure);
    return Promise.resolve(data).then(value=>{event('system-resume');return value;});}};
  const pasteboard={getSystemPasteboard(){assert.equal(this,pasteboard);event('system');return system;}};
  for(const [name,type]of [['MIMETYPE_TEXT_HTML','H'],['MIMETYPE_TEXT_PLAIN','T'],['MIMETYPE_PIXELMAP','P']])Object.defineProperty(pasteboard,name,{get(){event('mime',type);return mime[type];}});
  const pasteController={setStyledString(value){assert.equal(this,pasteController);event('clear',valueKind(value.value));}};
  const textController={setStyledString(value){assert.equal(this,textController);event('text',valueKind(value.value));}};
  const pixelController={deleteSpans(){assert.equal(this,pixelController);event('delete-spans');},
    addImageSpan(value,options){assert.equal(this,pixelController);event('image',valueKind(value),...Array.from(options.imageStyle.size));}};
  const prompt={showToast(options){assert.equal(this,prompt);event('toast',options.message.id,options.offset.dx,options.offset.dy);}};
  const context={getPromptAction(){assert.equal(this,context);event('prompt');return prompt;}};
  const owner={pasteController,pasteTextController:textController,pastePixelMapController:pixelController,
    handlerPasteHtmlData(value){assert.equal(this,owner);event('html',valueKind(value));},
    getUIContext(){assert.equal(this,owner);event('context');return context;}};
  const success={get SUCCESS(){event('success');return 'success';}};
  const hilog={get error(){event('read-error-call');return function(...args){event('error',...args);};}};
  const environment=vm.createContext({pasteboard,StyledString,PasteButtonOnClickResult:success,hilog});
  const factory=vm.runInContext(pairedSource?'(function(){return '+source+';})':'(function($local0){return '+source+';})',environment);
  const fn=pairedSource?factory.call(owner):factory(owner);
  let returned,escaped;
  try{returned=await fn({},result);}catch(error){escaped=error===failure?'original failure':error.name;}
  return {trace,returned,escaped};
}
const sequences=[[],['X'],['H'],['T'],['P'],['H','X'],['X','T'],['P','H','T'],['H','H'],['X','X']];
const records=[[],...sequences.map(value=>[value]),...sequences.flatMap(a=>sequences.map(b=>[a,b]))];
let cases=0,mutations=0;
for(const types of records)for(const result of ['success','denied',undefined,Symbol.for('status')])for(const mode of ['normal','length-coercion','system-reject','record-reject']){
  assert.deepEqual(await observe(actual,false,types,result,mode),await observe(expected,true,types,result,mode));++cases;
}
for(const types of records){
  const baseline=await observe(expected,true,types,'success','normal');
  for(let failAt=1;failAt<=baseline.trace.length+1;++failAt){
    assert.deepEqual(await observe(actual,false,types,'success','normal',failAt),await observe(expected,true,types,'success','normal',failAt),JSON.stringify({types,failAt}));++cases;
  }
}
for(const changed of [
  editExpression('('+actual+')',node=>node.type==='AssignmentExpression'&&node.right.type==='Literal'&&node.right.value===true,(node,text)=>'('+text(node.left)+' = false)',{first:true}),
  editExpression('('+actual+')',node=>node.type==='AwaitExpression'&&node.argument.type==='CallExpression'&&node.argument.arguments.length>0,(node,text)=>text(node.argument),{first:true})
]){
  assert.notDeepEqual(await observe(changed,false,[['H']],'success','normal'),await observe(expected,true,[['H']],'success','normal'));++mutations;
}
console.log(JSON.stringify({record:record.entity,pairedCallbackCases:cases,mutations,successFlagSlots:1,loopIndices:2,wholeRecordBehavior:false,platformExecution:false}));
