import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';

const rows=execFileSync(process.argv[2],['--original'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const failure=Symbol('conversion failure');
const primitive=[undefined,null,false,true,0,-0,1,-1,2.5,NaN,Infinity,-Infinity,-2147483648,'','0','2','bad',0n,1n,Symbol('value')];
const factories=primitive.map(value=>()=>value);
for(const result of [0,1,'2',NaN,1n,Symbol('converted'),{},failure]) {
  factories.push(trace=>({[Symbol.toPrimitive](hint) {
    trace.push(['primitive',hint]);if(result===failure)throw failure;return result;
  }}));
}
factories.push(trace=>Object.defineProperty({},Symbol.toPrimitive,{get(){trace.push('get primitive');throw failure;}}));
factories.push(trace=>({valueOf(){trace.push('valueOf');return {};},toString(){trace.push('toString');return '2';}}));
function outcome(fn,factory) {
  const trace=[],input=factory(trace);
  try {
    const result=fn(input);
    return {trace,kind:'return',value:result===input&&typeof input==='object'&&input!==null?'input':result};
  } catch(error) {return {trace,kind:'throw',value:error===failure?'failure':error.name};}
}
function equivalent(source,numeric) {
  const generated=Function('return ('+source+')')();
  // Independent source construction; no engine tree, retained domain or
  // projection data is used to compute the expected execution.
  const reference=numeric?function(params){const saved=+params;if(saved!==0)saved+1;return saved;}:
    function(params){const saved=params;if(saved!==0)saved+1;return saved;};
  for(const factory of factories)assert.deepEqual(outcome(generated,factory),outcome(reference,factory));
}
function nodes(root) {
  const result=[],pending=[root];
  while(pending.length) {
    const node=pending.pop();if(!node||typeof node!=='object')continue;
    if(node.type)result.push(node);
    for(const value of Object.values(node))if(value&&typeof value==='object') {
      if(Array.isArray(value))pending.push(...value);else pending.push(value);
    }
  }
  return result;
}
let comparisons=0,mutants=0;
for(const row of rows) {
  const wrapped='('+row.source+')',tree=parseJavaScript(wrapped),all=nodes(tree);
  const plus=all.filter(node=>node.type==='BinaryExpression'&&node.operator==='+');
  assert.equal(plus.length,row.numeric?0:1,'original normal-domain fact must reach emitted source');
  equivalent(row.source,row.numeric);comparisons+=factories.length;
  if(row.copies===0&&!row.reverse&&!row.split) {
    const node=row.numeric?all.find(node=>node.type==='UnaryExpression'&&node.operator==='+'):
      all.find(node=>node.type==='ExpressionStatement'&&node.expression.type==='BinaryExpression'&&node.expression.operator==='+');
    assert.ok(node);
    const replacement=row.numeric?wrapped.slice(node.argument.start,node.argument.end):';';
    const mutant=wrapped.slice(0,node.start)+replacement+wrapped.slice(node.end);
    assert.throws(()=>equivalent(mutant,row.numeric));++mutants;
  }
}
assert.equal(rows.length,96);
// Each fixture adds only SSA copies. Record actual dimensions and accounted
// construction cost; test the boundary and geometric growth without timing.
for(const numeric of [false,true])for(const reverse of [false,true])for(const split of [false,true])for(const arena of [false,true]) {
  const group=rows.filter(row=>row.numeric===numeric&&row.reverse===reverse&&row.split===split&&row.arena===arena).sort((a,b)=>a.copies-b.copies);
  for(const row of group) {
    assert.equal(row.operations,row.copies+6+(split?1:0));
    assert.equal(row.values,row.copies+5);
    assert.equal(row.uses,row.copies+8);
  }
  const byCount=new Map(group.map(row=>[row.copies,row]));
  for(const [before,after] of [[200,201],[200,400],[400,800]])for(const cost of ['work','bytes']) {
    const ratio=after===201?1.1:2.5;
    assert.ok(byCount.get(after)[cost]<=ratio*byCount.get(before)[cost],`${cost} must track graph dimensions`);
  }
}
console.log(`${rows.length} original-SSA sources / ${comparisons} independent execution comparisons / ${mutants} adverse mutants`);
