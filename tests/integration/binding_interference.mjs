import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
import {parseJavaScript} from './class_reference.mjs';
const records=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',timeout:60_000,maxBuffer:16*1024*1024}).trim().split('\n').map(JSON.parse);
assert.equal(records.length,252);
// Oracles describe the original storage semantics, never enter the engine.
const references=[
  function(input,report,flag){let current=input,scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current=input,scratch=current;++scratch;report(current);current=scratch;return current;},
  function(input,report,flag){let current=input,scratch;try{scratch=current;++scratch;current=scratch;}catch(error){return current;}return current;},
  function(input,report,flag){let current=input,scratch;try{scratch=current;++scratch;report();current=scratch;}catch(error){return current;}return current;},
  async function(input,report,flag){let current=input,scratch;try{scratch=current;++scratch;await report();current=scratch;}catch(error){return current;}return current;},
  function*(input,report,flag){let current=input,scratch;try{scratch=current;++scratch;yield scratch;current=scratch;}finally{report(current);}return current;},
  function(input,report,flag){let current=input,scratch=current;++scratch;if(flag)return current;current=scratch;return current;},
  function(input,report,flag){let current=0,scratch;while(!(current>=input)){scratch=current;++scratch;current=scratch;}return current;},
  function(input,report,flag){let current=input,scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current=input,scratch=current;++scratch;report(function(){return current;});current=scratch;return current;},
  function(input,report,flag){let current=input,scratch=current;{let current=100;++scratch;}current=scratch;return current;},
  function(input,report,flag){let current=input,scratch;report(scratch);scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current=input,scratch=current;++scratch;scratch=report(scratch);current=scratch;return current;},
  function(input,report,flag){let current=0;report(current);let scratch;report(scratch);current=input;scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current=0;report(current);for(;;){let scratch;report(scratch);current=input;scratch=current;++scratch;current=scratch;if(flag)break;flag=true;}return current;},
  function(input,report,flag){let current,scratch;report(scratch);current=input;scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current,scratch;(scratch=0,report());current=input;scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current,scratch;(scratch=0,report(scratch));current=input;scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current=input,scratch=current,snapshot;snapshot=++scratch;{current=snapshot;}return current;},
  function(input,report,flag){let current=input,scratch,snapshot;try{scratch=current;snapshot=++scratch;{current=snapshot;}}catch(error){return current;}return current;},
  function(input,report,flag){let current=input,scratch=current,snapshot;snapshot=++scratch;{if(snapshot){}let current=100;}current=scratch;return current;},
];
const primitives=[undefined,null,false,true,0,-0,1,-2,0n,2n,NaN,Infinity,'','7','bad',Symbol('input')];
let cases=0,mutations=0;
async function observe(fn,form,index,mode,flag,request){
  const events=[],original={},returned={};let input;
  if(index<primitives.length)input=primitives[index];
  else input={ [Symbol.toPrimitive](hint){events.push(['coerce',hint]);if(mode==='coercion-throw')throw original;return index===primitives.length?4:5n;} };
  const encode=value=>value===input&&typeof value==='object'&&value!==null?'input-object':value===original?'original-throw':value===returned?'return-object':value;
  const report=(...args)=>{events.push(['report',...args.map(value=>typeof value==='function'?['capture',encode(value())]:encode(value))]);if(mode==='report-throw')throw original;if(mode==='reject')return Promise.reject(original);return undefined;};
  // Avoid an unused rejected promise outside the awaited case.
  if(mode==='reject'&&form!==4)return;
  let result;
  try {
    if(form===5){
      const iterator=fn(input,report,flag),first=iterator.next();events.push(['yield',encode(first.value),first.done]);
      if(request==='throw')result=iterator.throw(original);else if(request==='return')result=iterator.return(returned);else result=iterator.next(returned);
      result=['completion',encode(result.value),result.done];
    } else result=await fn(input,report,flag);
    return {events,result:encode(result)};
  } catch(error){return {events,error:error===original?'original-throw':error.name};}
}
for(const record of records){
  parseJavaScript(record.source);const recovered=vm.runInNewContext(record.source);
  const inspect=parseJavaScript(record.source);const pending=[inspect];let hasScratch=false;
  while(pending.length){const node=pending.pop();if(node.type==='VariableDeclarator'&&node.id.name==='scratch')hasScratch=true;
    for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
  assert.equal(hasScratch,!record.merged);
  const inputs=record.form===7?[2,3,4,6,7,8,12,13]:Array.from({length:primitives.length+2},(_,i)=>i);
  for(const index of inputs)for(const mode of ['normal','coercion-throw','report-throw','reject'])for(const flag of [false,true])for(const request of record.form===5?['next','throw','return']:['next']){
    if(record.form===7&&primitives[index]===Infinity)continue;
    assert.deepEqual(await observe(recovered,record.form,index,mode,flag,request),await observe(references[record.form],record.form,index,mode,flag,request),JSON.stringify({form:record.form,index,mode,flag,request}));++cases;
  }
  if(record.count===0&&!record.reverse&&[1,3,4,5,6,9,10,11,17,20].includes(record.form)){
    // A semantically wrong coalescing remains valid JS: reuse current for every
    // scratch access while retaining the now-unused scratch declaration.
    const changed=record.source.replaceAll('scratch','currentAlias');
    const ast=parseJavaScript(changed),edits=[],work=[ast];
    while(work.length){const node=work.pop();if(node.type==='Identifier'&&node.name==='currentAlias')edits.push(node);
      for(const [key,value]of Object.entries(node)){if(node.type==='VariableDeclarator'&&key==='id')continue;
        if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    let mutant=changed;for(const edit of edits.sort((a,b)=>b.start-a.start))mutant=mutant.slice(0,edit.start)+'current'+mutant.slice(edit.end);
    parseJavaScript(mutant);const wrong=vm.runInNewContext(mutant);
    const mode=record.form===3?'report-throw':record.form===4?'reject':'normal';const request=record.form===5?'return':'next';
    assert.notDeepEqual(await observe(wrong,record.form,6,mode,true,request),await observe(references[record.form],record.form,6,mode,true,request));++mutations;
  }
  if(record.count===0&&!record.reverse&&[18,19].includes(record.form)) {
    const pending=[inspect];let update;
    while(pending.length){const node=pending.pop();if(node.type==='UpdateExpression')update=node;
      for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
    assert.ok(update);
    const mutant=record.source.slice(0,update.start)+record.source.slice(update.argument.start,update.argument.end)+record.source.slice(update.end);
    const wrong=vm.runInNewContext(mutant);
    assert.notDeepEqual(await observe(wrong,record.form,6,'normal',true,'next'),
        await observe(references[record.form],record.form,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&[13,14].includes(record.form)) {
    const missing=record.source.replaceAll('current = void 0;','');assert.notEqual(missing,record.source);
    const wrong=vm.runInNewContext(missing);
    assert.notDeepEqual(await observe(wrong,record.form,6,'normal',false,'next'),
        await observe(references[record.form],record.form,6,'normal',false,'next'));++mutations;
  }
}
const scaled=execFileSync(process.argv[2],['--scale'],{encoding:'utf8',timeout:60_000,maxBuffer:16*1024*1024}).trim().split('\n').map(JSON.parse);
// A stale pre-storage graph would drop this successful write but keep its RHS
// call. The retained-name read must observe the callback result, not old storage.
const stale=function(input,report){let current=input;++current;report(current);return current;};
assert.notDeepEqual(await observe(stale,12,6,'normal',true,'next'),
    await observe(references[12],12,6,'normal',true,'next'));++mutations;
assert.deepEqual(scaled.map(test=>test.count),[1,200,201,400,800]);
assert(scaled.at(-1).work<3*scaled.at(-2).work);
for(const test of scaled){parseJavaScript(test.source);const fn=vm.runInNewContext(test.source);
  for(const value of [0,2n,-0,'7']){const seen=[];fn(value,x=>seen.push(x));let expected=value;++expected;
    assert.equal(seen.length,test.count);for(const actual of seen)assert.equal(actual,expected);++cases;}}
console.log(JSON.stringify({sources:records.length+scaled.length,cases,mutations}));
