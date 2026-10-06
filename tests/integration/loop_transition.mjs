import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows = execFileSync(process.argv[2], ['--emit'], {encoding: 'utf8', timeout: 60_000, maxBuffer: 8_000_000}).trim().split('\n').map(JSON.parse);
assert.equal(rows.length, 108);
const object = {}, symbol = Symbol('state'), failure = Symbol('throw');
function reference(form, count) {
  const update = form === 5 ? 'next = await step(state)' : form === 6 ? 'next = yield step(state)' : 'next = step(state)';
  const early = form === 2 ? 'if(skip()){state=step(state);continue;}try{report(state);}finally{mark();}' : form === 3 ? 'if(skip())break;' : '';
  const work = update + ';' + (form === 4 ? 'if(skip())mark();else mark();' : 'mark();') + 'state=next;';
  const body = form === 7 ? '{let local=state;' + work + '}' : form === 8 ? 'try{' + work + '}finally{mark();}' : work;
  return Function('return (' + (form === 5 ? 'async function' : form === 6 ? 'function*' : 'function') +
    '(initial,step,report,mark,skip){let state=initial,next;try{while(state){for(let j=0;j<'+(count+1)+';j++)report(state);' + early + body + '}}catch(error){return state;}return state;})')();
}
async function observe(fn, form, initial, next, throwAt, interrupt = false) {
  const events = [], requests = []; let index = 0, skipped = false;
  const event = value => {events.push(value); assert.ok(events.length < 20_000); if(events.length === throwAt) throw failure;};
  const callbacks = [initial, function(previous) {
    assert.equal(this, undefined); assert.ok(Object.is(previous, index ? next[index-1] : initial));
    assert.ok(index < next.length); event(['step', previous]); const value=next[index++];
    if(form===5){queueMicrotask(()=>events.push(['job']));return Promise.resolve(value);} return value;
  }, function(value){assert.equal(this,undefined);event(['report',value]);},
  function(){assert.equal(this,undefined);event(['mark']);}, function(){assert.equal(this,undefined);event(['skip']);const value=!skipped;skipped=true;return value;}];
  let value=fn(...callbacks);
  if(form===6){const generator=value;let row=generator.next();while(!row.done){requests.push(row.value);row=interrupt?generator.throw(failure):generator.next(row.value);}value=row.value;}
  if(form===5)value=await value;
  return {value,events,requests};
}
function compare(actual, expected) {
  assert.ok(Object.is(actual.value,expected.value),'state/throw identity');
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
  assert.equal(loops.length,1);assert.equal(loops[0].type,row.native?'ForStatement':'WhileStatement');
  for(const [initial,next] of [[false,[]],[NaN,[]],[-0,[]],[object,[symbol,0]],[1n,[0n]]]){
    const baseline=await observe(original,row.form,initial,next,Infinity);
    compare(await observe(generated,row.form,initial,next,Infinity),baseline);++comparisons;
    const faults=[...new Set([1,row.count+1,row.count+2,baseline.events.length-1,baseline.events.length].filter(value=>value>0))];
    for(const at of faults){compare(await observe(generated,row.form,initial,next,at),await observe(original,row.form,initial,next,at));++comparisons;}
    if(row.form===6 && next.length){compare(await observe(generated,row.form,initial,next,Infinity,true),await observe(original,row.form,initial,next,Infinity,true));++comparisons;}
  }
  if(row.native && row.count===0){
    const test=loops[0].test, changed=row.source.slice(0,test.start)+'false'+row.source.slice(test.end);parseJavaScript(changed);
    const bad=Function('return ('+changed+')')();
    const actual=await observe(bad,row.form,object,[0],Infinity), expected=await observe(original,row.form,object,[0],Infinity);
    assert.throws(()=>compare(actual,expected));++mutations;
    if(row.form===0){
      const update=loops[0].update;
      assert.equal(update.type,'SequenceExpression');
      const parts=update.expressions.map(node=>row.source.slice(node.start,node.end));
      const wrong=[parts[0],parts.at(-1),...parts.slice(1,-1)].join(', ');
      const reordered=row.source.slice(0,update.start)+wrong+row.source.slice(update.end);parseJavaScript(reordered);
      const partial=Function('return ('+reordered+')')();
      const actual=await observe(partial,row.form,object,[0],3), expected=await observe(original,row.form,object,[0],3);
      assert.throws(()=>compare(actual,expected));++mutations;
    }
  }
}
console.log(JSON.stringify({sources:rows.length,comparisons,mutations,platformExecution:false}));
