import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', timeout: 60_000, maxBuffer: 16_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(rows.length, 156);
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
async function observe(fn, form, initial, next, throwAt, choice, interrupt = false, setupResult = initial, reject = false) {
  const events = [], requests = []; let at = 0, value, error;
  const event = value => {events.push(value); assert.ok(events.length < 20_000); if(events.length === throwAt) throw failure;};
  const callbacks = [initial, function(value) {
    assert.equal(this,undefined); assert.ok(Object.is(value,initial)); event(['setup',value]);
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
  const generated=Function('return ('+row.source+')')(), original=reference(row.form,row.count);
  const pending=[parseJavaScript(row.source)], loops=[];
  while(pending.length){const node=pending.pop();if(['ForStatement','WhileStatement'].includes(node.type))loops.push(node);
    for(const child of Object.values(node)){if(Array.isArray(child))pending.push(...child.filter(value=>value?.type));else if(child?.type)pending.push(child);}}
  assert.equal(loops.length,1); assert.equal(Boolean(loops[0].init),row.native,'entry projection preserves scope and handler fences');
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
