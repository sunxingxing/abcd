import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import vm from 'node:vm';
import {isDeepStrictEqual} from 'node:util';
import {parseJavaScript} from './class_reference.mjs';
const records=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',timeout:60_000,maxBuffer:16*1024*1024}).trim().split('\n').map(JSON.parse);
assert.equal(records.length,636);
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
  function(input,report,flag){let current=input,scratch=current;report(scratch);current=scratch;return current;},
  function(input,report,flag){let current=input,scratch=current,snapshot;snapshot=scratch;{if(snapshot){}let input=100;}current=scratch;return current;},
  function(input,report,flag){let current=input,scratch;scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current=report(input),scratch;scratch=current;++scratch;current=scratch;return current;},
  function(input,report,flag){let current;{let scratch;report(scratch=input);++scratch;current=scratch;++current;report(current);}{let current;}return current;},
  function(input,report,flag){let current,scratch;report(current=input);++current;report(scratch=current);{let current;++scratch;report(scratch);}current=scratch;return current;{let scratch;}},
  function(input,report,flag){let current,scratch;report(current=input);++current;report(scratch=current);++scratch;report(scratch);{let current;}current=scratch;return current;{let scratch;}},
  function(input,report,flag){let current;report(current=input);++current;let scratch=current;{let current=100;report(current);++scratch;report(scratch);}current=scratch;return current;{let scratch;}},
  function(input,report,flag){let current;report(current=input);++current;let scratch=current;{let current=100;report(current);++scratch;report(scratch);}current=scratch;return current;{let scratch;}},
];
references.push(function(input,report,flag){let current=input;report(current);++current;let scratch=current;{let current;{current=report(input);++current;report(current);}report(scratch);}return scratch;{let scratch;}});
references.push(function(input,report,flag){let current=input;report(current);++current;let scratch=current;{let current;current=report(input);while(!(current>=2)){++current;report(current);if(flag)break;flag=true;}report(scratch);}return scratch;{let scratch;}});
references.push(function(input,report,flag){let current=input;report(current);++current;let scratch=current;{let current;{current=report(input);++current;report(current);report(scratch);}report(scratch);}return scratch;{let scratch;}});
references.push(function(input,report,flag){let current=input;report(current);++current;let scratch=current;{let current;current=report(input);while(!(current>=2)){++current;report(current);report(scratch);if(flag)break;flag=true;}report(scratch);}return scratch;{let scratch;}});
references.push(function(input,report,flag){let current=report(input),scratch=current;{let current=scratch;report(current);}return current;});
references.push(function(input,report,flag){let current=report(input),scratch=current;{let current=scratch;report(current);}return current;});
references.push(function(input,report,flag){let current=report(input),scratch=current;{let current=input;{let input=100;report(current);}report(scratch);}return current;});
references.push(function(input,report,flag){let current=report(input),scratch=current;{let current=input;{let input=100;report(current);}let snapshot=scratch;report(snapshot);}return current;});
references.push(function(input,report,flag){let current;report(current=input);++current;let scratch=current;++scratch;let bridge=flag;report(bridge);{let current=flag;++current;bridge=current;++bridge;current=bridge;report(current);report(scratch);}current=scratch;return current;});
references.push(function(input,report,flag){let current;report(current=input);++current;let scratch=current;++scratch;let bridge=flag;report(bridge);{let current=flag;++current;bridge=current;++bridge;current=bridge;report(current);report(scratch);}current=scratch;return current;});
references.push(function(input,report,flag){let current;report(current=input);++current;let scratch=current;++scratch;let bridge=flag;report(bridge);{let current=flag;++current;bridge=current;++bridge;report(current);current=bridge;report(current);report(scratch);}current=scratch;return current;});
references.push(function(input,report,flag){let current;report(current=input);++current;let bridge=flag;report(bridge);let scratch=-current;{if(scratch)report(flag);let current=flag;++current;bridge=current;++bridge;current=bridge;report(current);}return current;});
references.push(function(input,report,flag){let current;report(current=input);++current;let bridge=flag;report(bridge);let scratch=-current;{if(scratch)report(flag);let current=flag;++current;bridge=current;++bridge;current=bridge;report(current);}return current;});
references.push(function(input,report,flag){let current;report(current=input);++current;let bridge=flag;report(bridge);let scratch=-current;{if(scratch)report(flag);let current=flag;++current;bridge=current;++bridge;report(current);current=bridge;report(current);}return current;});
references.push(function(input,report,flag){let scratch;if(flag)scratch=input;else scratch=input;return scratch;});
references.push(function(input,report,flag){let scratch;if(flag)scratch=input;else scratch=flag;return scratch;});
references.push(function(input,report,flag){let scratch;report(scratch);if(flag)scratch=input;else scratch=input;return scratch;});
references.push(function(input,report,flag){let scratch;if(flag)scratch=input;else scratch=input;input=flag;return scratch;});
references.push(function(input,report,flag){let scratch=input;while(flag){scratch=input;flag=false;}return scratch;});
references.push(function(input,report,flag){let scratch;if(flag)scratch=(report(flag),input);else scratch=(report(flag),input);return scratch;});
references.push(function(input,report,flag){let scratch;if(flag)scratch=input;else scratch=input;return scratch;});
references.push(function(input,report,flag){let scratch;if(flag)scratch=input;else scratch=input;{let input=flag;report(input);return scratch;}});
references.push(function(input,report,flag){let scratch;if(flag)scratch=input;else scratch=input;return scratch;});
const primitives=[undefined,null,false,true,0,-0,1,-2,0n,2n,NaN,Infinity,'','7','bad',Symbol('input')];
let cases=0,mutations=0;
async function observe(fn,form,index,mode,flag,request){
  const events=[],original={},returned={};let input;
  if(index<primitives.length)input=primitives[index];
  else input={ [Symbol.toPrimitive](hint){events.push(['coerce',hint]);if(mode==='coercion-throw')throw original;return index===primitives.length?4:5n;} };
  const encode=value=>value===input&&typeof value==='object'&&value!==null?'input-object':value===original?'original-throw':value===returned?'return-object':value;
  const report=(...args)=>{events.push(['report',...args.map(value=>typeof value==='function'?['capture',encode(value())]:encode(value))]);if(mode==='report-throw')throw original;if(mode==='reject')return Promise.reject(original);return [24,34,35].includes(form)?input:[31,33,36,37].includes(form)?0:undefined;};
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
  const inspect=parseJavaScript(record.source);const pending=[inspect];let hasScratch=false,hasCurrent=false,currentDeclarations=0;
  while(pending.length){const node=pending.pop();if(node.type==='VariableDeclarator'){if(node.id.name==='scratch')hasScratch=true;if(node.id.name==='current'){hasCurrent=true;++currentDeclarations;}}
    for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
  if(record.form===31||record.form===33){
    const pending=[inspect];let native=false;
    while(pending.length){const node=pending.pop();if(node.type==='ForStatement'&&node.init?.type==='VariableDeclaration')native=true;
      for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
    assert.ok(native,'native loop declaration');
  }
  if(record.form>=44){
    const slots=inspect.body[0].expression.body.body.filter(node=>node.type==='VariableDeclaration').flatMap(node=>node.declarations.map(item=>item.id.name));
    assert.deepEqual(slots,record.merged?[]:['scratch']);
    const work=[inspect];let returned;while(work.length){const node=work.pop();if(node.type==='ReturnStatement')returned=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    assert.equal(returned.argument.name,record.merged?'input':'scratch');
  }else if(record.form>=38){
    const slots=inspect.body[0].expression.body.body.filter(node=>node.type==='VariableDeclaration').flatMap(node=>node.declarations.map(item=>item.id.name));
    assert.deepEqual(slots,record.merged?['current','bridge']:record.form>=41?['current','bridge','scratch']:['current','scratch','bridge']);
    assert.equal(currentDeclarations,record.merged?1:2);
  }else if(record.form===28||record.form===29||record.form>=30){
    const slots=inspect.body[0].expression.body.body.filter(node=>node.type==='VariableDeclaration').flatMap(node=>node.declarations.map(item=>item.id.name));
    assert.deepEqual(slots,record.merged?['current']:['current','scratch']);
  }else if(record.form===26||record.form===27){
    const slots=inspect.body[0].expression.body.body.filter(node=>node.type==='VariableDeclaration').flatMap(node=>node.declarations.map(item=>item.id.name));
    assert.deepEqual(slots,[record.slot]);
  }else assert.equal(hasScratch,!record.merged);
  if(record.form===21)assert.equal(hasCurrent,false);
  if(record.form===22)assert.equal(hasCurrent,true);
  const inputs=record.form===7?[2,3,4,6,7,8,12,13]:Array.from({length:primitives.length+2},(_,i)=>i);
  for(const index of inputs)for(const mode of ['normal','coercion-throw','report-throw','reject'])for(const flag of [false,true])for(const request of record.form===5?['next','throw','return']:['next']){
    if(record.form===7&&primitives[index]===Infinity)continue;
    assert.deepEqual(await observe(recovered,record.form,index,mode,flag,request),await observe(references[record.form],record.form,index,mode,flag,request),JSON.stringify({form:record.form,index,mode,flag,request}));++cases;
  }
  if(record.count===0&&!record.reverse&&[1,3,4,5,6,9,10,20,32,33,35,36,37,39,40,42,43].includes(record.form)){
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
    assert.notDeepEqual(await observe(wrong,record.form,6,mode,record.form>=38?false:record.form!==33,request),await observe(references[record.form],record.form,6,mode,record.form>=38?false:record.form!==33,request),`coalescing mutant ${record.form}`);++mutations;
  }
  if(record.count===0&&!record.reverse&&[36,37].includes(record.form)) {
    const wrong=function(input,report){let current=report(input),scratch=current;{let current=input;{let input=100;report(input);}report(scratch);}return current;};
    assert.notDeepEqual(await observe(wrong,record.form,6,'normal',true,'next'),await observe(references[record.form],record.form,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===17) {
    // The read of scratch's known 0 can now be replaced before coalescing.
    // Replacing it with the other binding's early undefined is still wrong.
    const wrong=function(input,report){let current,scratch;(scratch=0,report(current));current=input;scratch=current;++scratch;current=scratch;return current;};
    assert.notDeepEqual(await observe(wrong,17,6,'normal',true,'next'),
        await observe(references[17],17,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&[18,19,23,24,25,26,27,28,29,30,31,32,33,38,39,40,41,42,43].includes(record.form)) {
    const pending=[inspect];let update;
    while(pending.length){const node=pending.pop();if(node.type==='UpdateExpression')update=node;
      for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
    assert.ok(update);
    const mutant=record.source.slice(0,update.start)+record.source.slice(update.argument.start,update.argument.end)+record.source.slice(update.end);
    const wrong=vm.runInNewContext(mutant);
    assert.notDeepEqual(await observe(wrong,record.form,6,'normal',record.form<30,'next'),
        await observe(references[record.form],record.form,6,'normal',record.form<30,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form>=41&&record.form<=43) {
    const work=[inspect];let condition;
    while(work.length){const node=work.pop();if(node.type==='IfStatement')condition=node.test;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    assert.ok(condition);assert.equal(condition.type,record.form===41?'UnaryExpression':'Identifier');
    if(record.form===41)assert.equal(condition.operator,'-');
    if(record.form===41){const mutant=record.source.slice(0,condition.start)+'false'+record.source.slice(condition.end);const wrong=vm.runInNewContext(mutant);
      assert.notDeepEqual(await observe(wrong,41,6,'normal',false,'next'),await observe(references[41],41,6,'normal',false,'next'));++mutations;}
    if(record.form===43){const wrong=function(input,report,flag){let current;report(current=input);++current;let bridge=flag;report(bridge);let scratch=-current;{if(scratch)report(flag);let current=flag;++current;++current;report(current);report(current);}return current;};
      assert.notDeepEqual(await observe(wrong,43,6,'normal',false,'next'),await observe(references[43],43,6,'normal',false,'next'));++mutations;}
  }
  if(record.count===0&&!record.reverse&&record.form>=44&&record.form<=51&&record.form!==50){
    const work=[inspect];let returned;while(work.length){const node=work.pop();if(node.type==='ReturnStatement')returned=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    let source=record.source;
    if(record.form===46||record.form===49){const work=[inspect];let call;while(work.length){const node=work.pop();if(node.type==='CallExpression')call=node;for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
      assert.ok(call);source=source.slice(0,call.start)+'void 0'+source.slice(call.end);}
    else source=source.slice(0,returned.argument.start)+([44,48].includes(record.form)?'flag':'input')+source.slice(returned.argument.end);
    const wrong=vm.runInNewContext(source);
    const differences=[];for(const flag of [false,true])differences.push(!isDeepStrictEqual(await observe(wrong,record.form,6,'normal',flag,'next'),await observe(references[record.form],record.form,6,'normal',flag,'next')));
    assert.ok(differences.some(Boolean),`stable value mutation ${record.form}`);++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===40) {
    const wrong=function(input,report,flag){let current;report(current=input);++current;let scratch=current;++scratch;let bridge=flag;report(bridge);{let current=flag;++current;++current;report(current);report(current);report(scratch);}current=scratch;return current;};
    assert.notDeepEqual(await observe(wrong,40,6,'normal',false,'next'),await observe(references[40],40,6,'normal',false,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===26) {
    const wrong=function(input,report){let current;report(current=input);++current;report(current);{let current;++current;report(current);}return current;{let scratch;}};
    assert.notDeepEqual(await observe(wrong,26,6,'normal',true,'next'),
        await observe(references[26],26,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===29) {
    const wrong=function(input,report){let current;report(current=input);++current;{let current=100;report(current);++current;report(current);}return current;{let scratch;}};
    assert.notDeepEqual(await observe(wrong,29,6,'normal',true,'next'),
        await observe(references[29],29,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===25) {
    const wrong=function(input,report){let current;{let scratch;report(scratch=input);++scratch;current=scratch;++current;report(current);}{let current;}return scratch;};
    assert.notDeepEqual(await observe(wrong,25,6,'normal',true,'next'),
        await observe(references[25],25,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===22) {
    // Wrong late aliasing crosses the shadowing declaration's TDZ. Rename only
    // reads, preserving declarations so the mutant remains syntactically valid.
    const edits=[],work=[inspect];
    while(work.length){const node=work.pop();if(node.type==='Identifier'&&node.name==='current')edits.push(node);
      for(const [key,value]of Object.entries(node)){if(node.type==='VariableDeclarator'&&key==='id')continue;
        if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    let mutant=record.source;for(const edit of edits.sort((a,b)=>b.start-a.start))mutant=mutant.slice(0,edit.start)+'input'+mutant.slice(edit.end);
    parseJavaScript(mutant);const wrong=vm.runInNewContext(mutant);
    assert.notDeepEqual(await observe(wrong,22,6,'normal',true,'next'),await observe(references[22],22,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&record.form===21) {
    const wrong=function(input,report){report(input);return undefined;};
    assert.notDeepEqual(await observe(wrong,21,6,'normal',true,'next'),await observe(references[21],21,6,'normal',true,'next'));++mutations;
  }
  if(record.count===0&&!record.reverse&&[11,13,14].includes(record.form)) {
    // Initialization now survives as a per-read undefined value, independently
    // of storage. Prematurely substituting the later input must be detected.
    const work=[inspect];let initial;
    while(work.length){const node=work.pop();if(node.type==='UnaryExpression'&&node.operator==='void')initial=node;
      for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)work.push(child);}else if(value?.type)work.push(value);}}
    assert.ok(initial);
    const missing=record.source.slice(0,initial.start)+'input'+record.source.slice(initial.end);
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
const chains=execFileSync(process.argv[2],['--chains'],{encoding:'utf8',timeout:120_000,maxBuffer:16*1024*1024}).trim().split('\n').map(JSON.parse);
assert.equal(chains.length,24);
for(const test of chains){
  const ast=parseJavaScript(test.source),pending=[ast];let declarations=0;
  while(pending.length){const node=pending.pop();if(node.type==='VariableDeclarator')++declarations;
    for(const value of Object.values(node)){if(Array.isArray(value)){for(const child of value)if(child?.type)pending.push(child);}else if(value?.type)pending.push(value);}}
  assert.equal(declarations,test.conflict?2:1);
  const names=Array.from({length:test.count},(_,i)=>'slot'+i);
  let body='let current=input;'+names.map(name=>'let '+name+';').join('');
  for(let i=0;i<names.length;++i)body+=names[i]+'='+(i?names[i-1]:'current')+';report('+names[i]+');';
  body+='++'+names.at(-1)+';'+(test.conflict?'report(current);':'')+'current='+names.at(-1)+';return current;';
  const reference=Function('return function(input,report,flag){'+body+'}')();
  const recovered=vm.runInNewContext(test.source);
  for(let index=0;index<primitives.length+2;++index)for(const mode of ['normal','coercion-throw','report-throw'])for(const flag of [false,true]){
    assert.deepEqual(await observe(recovered,-1,index,mode,flag,'next'),await observe(reference,-1,index,mode,flag,'next'),JSON.stringify(test));++cases;
  }
  if(test.count===2&&!test.reverse){
    if(test.conflict){
      // Every affinity pair can be safe while a non-affinity end pair conflicts.
      // Blind contraction must expose the updated value at the old-root read.
      const wrong=function(input,report){let current=input;report(current);report(current);++current;report(current);return current;};
      assert.notDeepEqual(await observe(wrong,-1,6,'normal',true,'next'),await observe(reference,-1,6,'normal',true,'next'));++mutations;
    }else{
      const wrong=function(input,report){report(input);report(input);return input;};
      assert.notDeepEqual(await observe(wrong,-1,6,'normal',true,'next'),await observe(reference,-1,6,'normal',true,'next'));++mutations;
    }
  }
}
console.log(JSON.stringify({sources:records.length+scaled.length+chains.length,cases,mutations}));
