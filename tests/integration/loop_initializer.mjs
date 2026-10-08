import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', timeout: 60_000, maxBuffer: 32_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(rows.length, 276);
const object = {}, symbol = Symbol('state'), failure = Symbol('throw');
function reference(form, count) {
  const setup = form === 3 ? 'await setup(next)' : form === 4 ? 'yield setup(next)' : 'setup(next)';
  const init = (form === 11 ? 'gate=mark();' : '') + 'next=initial;' + 'mark();'.repeat(form === 12 ? count+1 : 1) + (form === 2 ? 'if(choose())state=' + setup + ';else state=next;' :
    form === 9 ? 'state=local;let local=initial;' : 'state=' + setup + ';');
  const work = 'for(let i=0;i<' + (form === 12 ? 1 : count+1) + ';i++)report(state);' +
    (form === 8 ? 'if(choose()){state=step(state);continue;}try{mark();}finally{mark();}' : '') + 'state=step(state);';
  let loop = 'while(' + (form === 11 ? 'state&&gate' : 'state') + '){' + work + '}';
  if (form === 5) loop = '{let local;' + loop + '}';
  if (form === 10) loop = 'entry:{' + loop + 'break entry;}';
  const protectedBody = form === 6 ? 'try{' + loop + '}' : 'try{' + init + (form === 7 ? 'let local=mark();' : '') + loop + '}';
  return Function('return (' + (form === 3 ? 'async function' : form === 4 ? 'function*' : 'function') +
    '(initial,setup,report,step,mark,choose){let state=initial,next' + (form === 11 ? ',gate' : '') + ';' + (form === 6 ? init : '') +
    protectedBody + 'catch(error){return state;}return state;})')();
}
function bindingReference(form,count) {
  const value=form===1?'await setup(next)':form===2?'yield setup(next)':form===4?'setup(state)':'setup(next)';
  return Function('return ('+(form===1?'async function':form===2?'function*':'function')+
    '(initial,setup,report,step,mark,choose){let state,next;initial;try{next='+(form===5?'state':'initial')+
    ';mark();state='+value+';while(state){'+'report(state);'.repeat(count+1)+
    'state=step(state);}}catch(error){return;}return'+(form===3?' state':'')+';})')();
}
async function observe(fn, form, initial, next, throwAt, choice, interrupt = false, setupResult = initial, reject = false, ...setupInputs) {
  // JS defaults also replace explicitly passed undefined. Preserve its presence.
  const setupInput=setupInputs.length?setupInputs[0]:initial;
  const events = [], requests = []; let at = 0, value, error;
  const event = value => {events.push(value); assert.ok(events.length < 20_000); if(events.length === throwAt) throw failure;};
  const callbacks = [initial, function(value) {
    assert.equal(this,undefined); assert.ok(Object.is(value,setupInput)); event(['setup',value]);
    if(form===3){queueMicrotask(()=>events.push(['job']));return reject ? Promise.reject(failure) : Promise.resolve(setupResult);}return setupResult;
  }, function(value){assert.equal(this,undefined);event(['report',value]);}, function(previous){
    assert.equal(this,undefined);assert.ok(Object.is(previous,at ? next[at-1] : setupResult));
    assert.ok(at < next.length);event(['step',previous]);return next[at++];
  }, function(){assert.equal(this,undefined);event(['mark']);return choice;}, function(){assert.equal(this,undefined);event(['choose']);return choice;}];
  try {
    value=fn(...callbacks);
    if(form===4){const generator=value;let row=generator.next();while(!row.done){requests.push(row.value);row=interrupt==='return'?generator.return(failure):interrupt?generator.throw(failure):generator.next(row.value);}value=row.value;}
    if(form===3)value=await value;
  } catch (caught) {error=caught;}
  return {value,error,events,requests};
}
function compare(actual, expected) {
  assert.ok(Object.is(actual.value,expected.value),'state identity after initialization and exceptions');
  assert.ok(Object.is(actual.error,expected.error),'throw identity and handler ownership');
  assert.equal(actual.events.length,expected.events.length);
  for(let i=0;i<actual.events.length;i++){assert.equal(actual.events[i][0],expected.events[i][0]);assert.ok(Object.is(actual.events[i][1],expected.events[i][1]));}
  assert.equal(actual.requests.length,expected.requests.length);
  for(let i=0;i<actual.requests.length;i++)assert.ok(Object.is(actual.requests[i],expected.requests[i]));
}
let comparisons=0,mutations=0;
for(const row of rows){
  const binding=row.binding!==undefined;
  const generated=Function('return ('+row.source+')')(), original=binding?bindingReference(row.binding,row.count):reference(row.form,row.count);
  const pending=[parseJavaScript(row.source)], loops=[];
  while(pending.length){const node=pending.pop();if(['ForStatement','WhileStatement'].includes(node.type))loops.push(node);
    for(const child of Object.values(node)){if(Array.isArray(child))pending.push(...child.filter(value=>value?.type));else if(child?.type)pending.push(child);}}
  assert.equal(loops.length,row.binding===9?2:1);
  if(binding) {
    assert.ok(loops[0].init);assert.equal(loops[0].init.type==='VariableDeclaration',row.lexical);
    if(row.lexical)assert.equal(loops[0].init.kind,'let');
    // Forward intermediate copies and coalesce the non-interfering final copy;
    // the original initializer evaluation remains in one loop binding.
    if(row.binding===7)assert.equal(loops[0].init.declarations.length,1);
    const form=row.binding===1?3:row.binding===2?4:0;
    const run=(fn,initial,next,fault,interrupt=false,reject=false)=>observe(fn,form,initial,next,fault,true,interrupt,initial,reject,
      row.binding===4 || row.binding===5 ? undefined : initial);
    for(const [initial,next] of [[false,[]],[NaN,[]],[-0,[]],[object,[symbol,0]],[1n,[0n]]]) {
      const baseline=await run(original,initial,next,Infinity);
      assert.ok(baseline.events.some(event=>event[0]==='setup'),'reference must execute the producer; a swallowed harness assertion is not equivalence');
      for(const fault of [...new Set([Infinity,1,2,3,4,baseline.events.length-1,baseline.events.length].filter(value=>value>0))]) {
        compare(await run(generated,initial,next,fault),await run(original,initial,next,fault));++comparisons;
      }
      if(form===3){compare(await run(generated,initial,next,Infinity,false,true),await run(original,initial,next,Infinity,false,true));++comparisons;}
      if(form===4)for(const request of ['return',true]){compare(await run(generated,initial,next,Infinity,request),await run(original,initial,next,Infinity,request));++comparisons;}
    }
    if(row.lexical && row.count===0) {
      // Value normalization may introduce an earlier inert declarator. Mutate
      // the state producer, rather than an equivalent undefined initialization.
      const init=loops[0].init.declarations.find(value=>value.id.name==='state').init;
      for(const replacement of ['void 0','(mark(), '+row.source.slice(init.start,init.end)+')']) {
        const changed=row.source.slice(0,init.start)+replacement+row.source.slice(init.end);parseJavaScript(changed);
        const bad=Function('return ('+changed+')')();
        const actual=await run(bad,object,[symbol,0],Infinity),expected=await run(original,object,[symbol,0],Infinity);
        assert.throws(()=>compare(actual,expected),assert.AssertionError,JSON.stringify({binding:row.binding,replacement}));++mutations;
      }
    }
    continue;
  }
  assert.equal(Boolean(loops[0].init),row.native,'entry projection preserves scope and handler fences');
  for(const choice of [false,true]) for(const [initial,next] of [[false,[]],[NaN,[]],[-0,[]],[object,[symbol,0]],[1n,[0n]]]){
    const baseline=await observe(original,row.form,initial,next,Infinity,choice);
    compare(await observe(generated,row.form,initial,next,Infinity,choice),baseline);++comparisons;
    const faults=[...new Set([1,2,3,4,row.count+4,baseline.events.length-1,baseline.events.length].filter(value=>value>0))];
    for(const fault of faults){compare(await observe(generated,row.form,initial,next,fault,choice),await observe(original,row.form,initial,next,fault,choice));++comparisons;}
    if(row.form===4){compare(await observe(generated,row.form,initial,next,Infinity,choice,true),await observe(original,row.form,initial,next,Infinity,choice,true));++comparisons;}
    if(row.form===4){compare(await observe(generated,row.form,initial,next,Infinity,choice,'return'),await observe(original,row.form,initial,next,Infinity,choice,'return'));++comparisons;}
    if(row.form===3){compare(await observe(generated,row.form,initial,next,Infinity,choice,false,initial,true),await observe(original,row.form,initial,next,Infinity,choice,false,initial,true));++comparisons;}
  }
  if(row.form===0) for(const fault of [Infinity,2,3]) {
    compare(await observe(generated,row.form,object,[0],fault,true,false,symbol),await observe(original,row.form,object,[0],fault,true,false,symbol));++comparisons;
  }
  if(row.native && row.count===0){
    const init=loops[0].init, changed=row.source.slice(0,init.start)+'false'+row.source.slice(init.end);parseJavaScript(changed);
    const bad=Function('return ('+changed+')')();
    const actual=await observe(bad,row.form,object,[0],Infinity,true), expected=await observe(original,row.form,object,[0],Infinity,true);
    assert.throws(()=>compare(actual,expected));++mutations;
    const test=loops[0].test;
    const repeated=row.source.slice(0,test.start)+'('+row.source.slice(init.start,init.end)+', '+row.source.slice(test.start,test.end)+')'+row.source.slice(test.end);
    parseJavaScript(repeated); const again=Function('return ('+repeated+')')();
    const duplicate=await observe(again,row.form,false,[],Infinity,true), once=await observe(original,row.form,false,[],Infinity,true);
    assert.throws(()=>compare(duplicate,once));++mutations;
  }
}
console.log(JSON.stringify({sources:rows.length,comparisons,mutations,platformExecution:false}));
