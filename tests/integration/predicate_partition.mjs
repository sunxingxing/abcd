import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
import {editExpression, memberName} from './expression_edits.mjs';

const rows=execFileSync(process.argv[2],['--source'],{encoding:'utf8',timeout:60000,maxBuffer:16*1024*1024})
  .trim().split('\n').map(line=>JSON.parse(line));
assert.equal(rows.length,180);
const failures=[undefined,null,0,Symbol('fault'),{}];
const payloads=[undefined,null,false,true,0,1,NaN,Infinity,1n,Symbol('value'),{}];
function observe(fn,row,payload,failAt,failure,coerce=false) {
  const trace=[];let result,thrown=false;
  const event=name=>{trace.push(name);if(trace.length===failAt)throw failure;};
  const converted={valueOf(){event('valueOf');return 1;}};
  const input={get value(){event('value');return coerce?converted:payload;},get effect(){event('effect');return payload;}};
  try{result=fn(input);}catch(error){result=error;thrown=true;}
  return {result,thrown,trace};
}
function reference(row) {
  return input=>{
    try {
      const value=input.value;
      if(row.effect)void input.effect;
      let result=row.coercing?value==true:value===true;
      for(let i=0;i<row.count;++i)result=!result;
      return result?true:false;
    }catch(error){return error;}
  };
}
function compare(fn,row,expected=reference(row)) {
  let cases=0;
  for(const payload of payloads) {
    const a=observe(fn,row,payload),b=observe(expected,row,payload);
    assert.equal(a.thrown,b.thrown);assert.ok(Object.is(a.result,b.result));assert.deepEqual(a.trace,b.trace);++cases;
  }
  const baseline=observe(expected,row,null,undefined,undefined,true);
  for(let failAt=0;failAt<=baseline.trace.length;++failAt)for(const failure of failures) {
    const a=observe(fn,row,null,failAt,failure,true),b=observe(expected,row,null,failAt,failure,true);
    assert.equal(a.thrown,b.thrown);assert.ok(Object.is(a.result,b.result));assert.deepEqual(a.trace,b.trace);++cases;
  }
  return cases;
}
let cases=0,mutations=0;
for(const row of rows) {
  assert.doesNotMatch(row.source,/INCOMPLETE|UNRECOVERED/);
  parseJavaScript(row.source);const fn=Function('return '+row.source)();cases+=compare(fn,row);
  // Mutate actual generated expressions, not private temporary spelling. All
  // variants remain valid JS; evaluation count and coercion must be observed.
  if(row.count!==0)continue;
  const read=node=>node.type==='MemberExpression'&&memberName(node)==='value';
  for(const replacement of [()=> 'undefined',(node,text)=>'('+text(node)+', '+text(node)+')']) {
    const changed=editExpression(row.source,read,replacement);
    assert.throws(()=>compare(Function('return '+changed)(),row),assert.AssertionError);++mutations;
  }
  if(row.coercing) {
    const changed=editExpression(row.source,node=>node.type==='BinaryExpression'&&['==','!='].includes(node.operator),
      (node,text)=>'('+text(node.left)+' '+node.operator+'= '+text(node.right)+')');
    assert.throws(()=>compare(Function('return '+changed)(),row),assert.AssertionError);++mutations;
  }
}
console.log(JSON.stringify({decodedSources:rows.length,behaviorCases:cases,validMutations:mutations}));
