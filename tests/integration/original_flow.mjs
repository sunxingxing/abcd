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

const projected=execFileSync(process.argv[3],['--source'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
function projectedOutcome(fn,factory,loop,rounds) {
  const trace=[],objects=[];let calls=0;
  const input=()=>{
    trace.push(['call',++calls]);
    if(loop&&calls%2===1)return (calls+1)/2<=rounds;
    const value=factory(trace);if(value!==null&&(typeof value==='object'||typeof value==='function'))objects.push(value);
    return value;
  };
  try {
    const value=fn(input),index=objects.indexOf(value);
    return {trace,kind:'return',value:index>=0?['input object',index]:value};
  }catch(error){return {trace,kind:'throw',value:error===failure?'failure':error.name};}
}
const projectedFactories=[...factories,()=>{throw failure;}];
function projectedEquivalent(source,form) {
  const generated=Function('return ('+source+')')(),loop=form>=2,numeric=form%2===0;
  // Independent language program, with success-only assignment and catch
  // reads. No domain/effect/def-use facts from the engine enter this oracle.
  const reference=function(input) {
    let saved=7;
    try {
      if(loop)while(input()){saved=numeric?+input():input();-saved;}
      else saved=numeric?+input():input();
    }catch(error){-saved;}
    -saved;return saved;
  };
  for(const factory of projectedFactories)for(const rounds of (loop?[0,1,2,3]:[1]))
    assert.deepEqual(projectedOutcome(generated,factory,loop,rounds),projectedOutcome(reference,factory,loop,rounds));
}
let projectedComparisons=0,projectedMutants=0;
for(const row of projected) {
  const source='('+row.source+')',all=nodes(parseJavaScript(source));
  const negative=all.filter(node=>node.type==='UnaryExpression'&&node.operator==='-');
  assert.equal(negative.length,row.form%2===0?0:row.form>=2?3:1,
    'success-definition domains must reach actual emitted source');
  projectedEquivalent(row.source,row.form);
  projectedComparisons+=projectedFactories.length*(row.form>=2?4:1);
  const declaration=all.find(node=>node.type==='VariableDeclarator'&&node.init?.type==='Literal'&&node.init.value===7);
  assert.ok(declaration,'live initial value remains across failed writes');
  const deadInit=source.slice(0,declaration.init.start)+'undefined'+source.slice(declaration.init.end);
  assert.throws(()=>projectedEquivalent(deadInit,row.form));++projectedMutants;
  if(row.form%2===0) {
    const conversion=all.find(node=>node.type==='UnaryExpression'&&node.operator==='+');
    const assignment=all.find(node=>node.type==='AssignmentExpression'&&node.right===conversion);
    assert.ok(conversion&&assignment);
    const argument=source.slice(conversion.argument.start,conversion.argument.end);
    const target=source.slice(assignment.left.start,assignment.left.end);
    for(const replacement of [argument,`(${target}=${argument},+${target})`]) {
      const mutant=source.slice(0,conversion.start)+replacement+source.slice(conversion.end);
      assert.throws(()=>projectedEquivalent(mutant,row.form));++projectedMutants;
    }
  }else {
    let mutant=source;
    for(const node of negative.sort((a,b)=>b.start-a.start))
      mutant=mutant.slice(0,node.start)+source.slice(node.argument.start,node.argument.end)+mutant.slice(node.end);
    assert.throws(()=>projectedEquivalent(mutant,row.form));++projectedMutants;
  }
}
assert.equal(projected.length,8);
console.log(`${projected.length} projected success/exception/loop sources / ${projectedComparisons} independent comparisons / ${projectedMutants} adverse mutants`);
