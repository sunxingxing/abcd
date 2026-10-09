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

const definitions=execFileSync(process.argv[2],['--definitions'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
let definitionComparisons=0,definitionMutants=0;
function definitionEquivalent(source,conditional) {
  const generated=Function('return ('+source+')')();
  const reference=conditional?function(params){if(params===0){const saved=+params;saved+1;return saved;}return params;}:
    function(params){const saved=+(params===0);saved+1;return saved;};
  for(const factory of factories)assert.deepEqual(outcome(generated,factory),outcome(reference,factory));
}
for(const row of definitions) {
  definitionEquivalent(row.source,row.conditional);definitionComparisons+=factories.length;
  const source='('+row.source+')';
  const comparison=nodes(parseJavaScript(source)).find(node=>node.type==='BinaryExpression'&&['===','!=='].includes(node.operator));
  assert.ok(comparison,'strict comparison remains observable');
  const replacement=row.conditional?(comparison.operator==='==='?'true':'false'):source.slice(comparison.left.start,comparison.left.end);
  const mutant=source.slice(0,comparison.start)+replacement+source.slice(comparison.end);
  assert.throws(()=>definitionEquivalent(mutant,row.conditional));++definitionMutants;
}
assert.equal(definitions.length,18);
console.log(`${definitions.length} checked successful-definition sources / ${definitionComparisons} independent comparisons / ${definitionMutants} adverse mutants`);

// Independently execute current catch identities and undefined declaration
// completion over repeated entries. These are source-adapter fixtures, not
// decoded original-SSA certificates; production never receives this program.
const activations=execFileSync(process.argv[3],['--activation'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
function activationOutcome(fn,rounds) {
  let calls=0;const errors=[],trace=[];
  const input=function(...args) {
    if(args.length){trace.push(['catch',errors.indexOf(args[0]),args[1]]);return;}
    ++calls;
    if(calls%2===1)return (calls+1)/2<=rounds;
    const error={iteration:calls/2};errors.push(error);throw error;
  };
  try{return {trace,result:fn(input)};}catch(error){return {trace,error:errors.indexOf(error),name:error.name};}
}
let activationComparisons=0,activationMutants=0;
const activationReference=function(input){while(input()){try{input();}catch(error){let local;input(error,local);}}};
for(const row of activations) {
  const generated=Function('return ('+row.source+')')();
  for(const rounds of [0,1,2,3,8]) {
    assert.deepEqual(activationOutcome(generated,rounds),activationOutcome(activationReference,rounds));++activationComparisons;
  }
  const wrapped='('+row.source+')',ast=nodes(parseJavaScript(wrapped));
  const observed=ast.find(node=>node.type==='CallExpression'&&node.arguments.length===2);
  assert.ok(observed);
  for(const [argument,replacement] of [[observed.arguments[0],'undefined'],[observed.arguments[1],'1']]) {
    const mutant=wrapped.slice(0,argument.start)+replacement+wrapped.slice(argument.end);
    const fn=Function('return '+mutant)();
    assert.notDeepEqual(activationOutcome(fn,3),activationOutcome(activationReference,3));++activationMutants;
  }
}
assert.equal(activations.length,8);
console.log(`${activations.length} repeated catch/local activation sources / ${activationComparisons} independent comparisons / ${activationMutants} adverse mutants`);

const exits=execFileSync(process.argv[3],['--scope-exits'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
function exitOutcome(fn,form,factory,rounds) {
  const trace=[],objects=[];let calls=0;
  const normalize=value=>objects.includes(value)?['input object',objects.indexOf(value)]:value;
  const input=function(...args) {
    if(args.length){trace.push(['catch',this===undefined,...args.map(normalize)]);return;}
    trace.push(['call',++calls,this===undefined]);
    if(form<=1&&calls%2===1)return (calls+1)/2<=rounds;
    if(form===2&&calls===2)return failure;
    const value=factory(trace);if(value!==null&&(typeof value==='object'||typeof value==='function'))objects.push(value);
    return value;
  };
  try{return {trace,kind:'return',value:normalize(fn(input))};}
  catch(error){return {trace,kind:'throw',value:error===failure?'failure':normalize(error)};}
}
const exitReferences=[
  function(input){let saved;while(input()){{let local=input();saved=local;break;}}return saved;},
  function(input){let saved;while(input()){{let local=input();saved=local;continue;}}return saved;},
  function(input){let saved;try{let local=input();saved=local;throw input();}catch(error){input(error,saved);}return saved;},
  function(input){{let local=input();return local;}},
];
let exitComparisons=0,exitMutants=0;
function checkExit(source,form) {
  const generated=Function('return ('+source+')')();
  for(const factory of [...factories,()=>{throw failure;}])for(const rounds of (form<=1?[0,1,2,3]:[1]))
    assert.deepEqual(exitOutcome(generated,form,factory,rounds),exitOutcome(exitReferences[form],form,factory,rounds));
}
for(const row of exits) {
  checkExit(row.source,row.form);exitComparisons+=(factories.length+1)*(row.form<=1?4:1);
  const wrapped='('+row.source+')',all=nodes(parseJavaScript(wrapped));
  const returned=all.find(node=>node.type==='ReturnStatement'&&node.argument);
  assert.ok(returned);
  const lostValue=wrapped.slice(0,returned.argument.start)+'undefined'+wrapped.slice(returned.argument.end);
  assert.throws(()=>checkExit(lostValue,row.form));++exitMutants;
  if(row.form===2) {
    const thrown=all.find(node=>node.type==='ThrowStatement');assert.ok(thrown);
    const lostThrow=wrapped.slice(0,thrown.start)+';'+wrapped.slice(thrown.end);
    assert.throws(()=>checkExit(lostThrow,row.form));++exitMutants;
  }
}
assert.equal(exits.length,32);
console.log(`${exits.length} scoped jump/throw/return sources / ${exitComparisons} independent comparisons / ${exitMutants} adverse mutants`);
