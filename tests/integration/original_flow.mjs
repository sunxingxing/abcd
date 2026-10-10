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
  assert.equal(nodes(parseJavaScript('('+row.source+')')).filter(node=>node.type==='DebuggerStatement').length,row.action?1:0);
  definitionEquivalent(row.source,row.conditional);definitionComparisons+=factories.length;
  const source='('+row.source+')';
  const comparison=nodes(parseJavaScript(source)).find(node=>node.type==='BinaryExpression'&&['===','!=='].includes(node.operator));
  assert.ok(comparison,'strict comparison remains observable');
  const replacement=row.conditional?(comparison.operator==='==='?'true':'false'):source.slice(comparison.left.start,comparison.left.end);
  const mutant=source.slice(0,comparison.start)+replacement+source.slice(comparison.end);
  assert.throws(()=>definitionEquivalent(mutant,row.conditional));++definitionMutants;
}
assert.equal(definitions.length,104);
console.log(`${definitions.length} checked successful-definition sources / ${definitionComparisons} independent comparisons / ${definitionMutants} adverse mutants`);
for(const action of [false,true])for(const reverse of [false,true])for(const split of [false,true])for(const arena of [false,true]) {
  const series=definitions.filter(row=>!row.conditional&&row.action===action&&row.reverse===reverse&&row.split===split&&row.arena===arena&&row.copies>=200);
  assert.deepEqual(series.map(row=>row.copies),[200,201,400,800]);
  for(let i=1;i<series.length;++i) {
    assert.ok(series[i].work<series[i-1].work*3);
    assert.ok(series[i].allocated_bytes<series[i-1].allocated_bytes*3);
  }
}
console.log('inline-result 200/201/400/800 admitted cost scaling passed');
const catches=execFileSync(process.argv[2],['--catch'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const catchFactories=[...factories,trace=>({[Symbol.toPrimitive](hint){trace.push(['throw number',hint]);throw 2;}})];
const catchReferences=[
  function(){try{throw 7;}catch(error){-error;return error;}},
  function(){try{throw true;}catch(error){-error;return error;}},
  function(params){try{throw params;}catch(error){-error;return error;}},
  function(params){try{+params;throw 7;}catch(error){-error;return error;}},
  function(params){let saved=7;try{saved=+params;throw saved;}catch(error){-error;return saved;}},
];
function catchEquivalent(source,form) {
  const generated=Function('return ('+source+')')();
  for(const factory of catchFactories)assert.deepEqual(outcome(generated,factory),outcome(catchReferences[form],factory));
}
let catchMutants=0;
for(const row of catches) {
  catchEquivalent(row.source,row.form);
  const wrapped='('+row.source+')',all=nodes(parseJavaScript(wrapped));
  const negative=all.find(node=>node.type==='UnaryExpression'&&node.operator==='-');
  assert.equal(Boolean(negative),row.form>=2,'implicit failures keep their own payload and conversion effects');
  if(negative) {
    const mutant=wrapped.slice(0,negative.start)+wrapped.slice(negative.argument.start,negative.argument.end)+wrapped.slice(negative.end);
    assert.throws(()=>catchEquivalent(mutant,row.form));++catchMutants;
  }
  if(row.form===4) {
    const assignment=all.find(node=>node.type==='AssignmentExpression'&&node.right.type==='UnaryExpression'&&node.right.operator==='+');
    assert.ok(assignment);
    const target=wrapped.slice(assignment.left.start,assignment.left.end),value=wrapped.slice(assignment.right.argument.start,assignment.right.argument.end);
    const mutant=wrapped.slice(0,assignment.start)+`(${target}=${value},${target}=+${target})`+wrapped.slice(assignment.end);
    assert.throws(()=>catchEquivalent(mutant,row.form));++catchMutants;
  }
}
assert.equal(catches.length,56);
for(const split of [false,true])for(const reallocated of [false,true]) {
  const series=catches.filter(row=>row.form===0&&row.split===split&&row.reallocated===reallocated&&row.copies>=200);
  assert.deepEqual(series.map(row=>row.copies),[200,201,400,800]);
  for(let i=1;i<series.length;++i)for(const cost of ['work','bytes'])
    assert.ok(series[i][cost]<series[i-1][cost]*(series[i].copies===201?1.1:3));
}
console.log(`${catches.length} original catch/implicit failure/commit sources / ${catches.length*catchFactories.length} independent comparisons / ${catchMutants} adverse mutants`);

// Source-adapter model: observable declarative commits are distinct from user
// callbacks. Its synthetic original envelope is not a raw IR certificate.
const effectSources=execFileSync(process.argv[3],['--effects'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
const effectReference=function(input){let other=7;let saved=7;saved=+input();other=7;-saved;return saved;};
function effectEquivalent(source) {
  const generated=Function('return ('+source+')')();
  for(const factory of factories) {
    const reference=function(params){return effectReference(()=>params);};
    const actual=function(params){return generated(()=>params);};
    assert.deepEqual(outcome(actual,factory),outcome(reference,factory));
  }
}
let effectMutants=0;
for(const row of effectSources) {
  effectEquivalent(row.source);
  const wrapped='('+row.source+')',all=nodes(parseJavaScript(wrapped));
  assert.equal(all.filter(node=>node.type==='UnaryExpression'&&node.operator==='-').length,0);
  assert.ok(all.some(node=>node.type==='AssignmentExpression'&&node.left.name==='other'),
    'absence of user callbacks does not license erasing an observable commit');
  const conversion=all.find(node=>node.type==='UnaryExpression'&&node.operator==='+');assert.ok(conversion);
  const mutant=wrapped.slice(0,conversion.start)+wrapped.slice(conversion.argument.start,conversion.argument.end)+wrapped.slice(conversion.end);
  assert.throws(()=>effectEquivalent(mutant));++effectMutants;
}
assert.equal(effectSources.length,2);
console.log(`${effectSources.length} completion-effect sources / ${effectSources.length*factories.length} independent comparisons / ${effectMutants} adverse mutants`);

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
const narrowed=execFileSync(process.argv[3],['--projected-lifetime'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
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
for(const row of [...exits,...narrowed]) {
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
assert.equal(exits.length,32);assert.equal(narrowed.length,48);
console.log(`${exits.length+narrowed.length} scoped jump/throw/return and narrowed sources / ${exitComparisons} independent comparisons / ${exitMutants} adverse mutants`);

for(let form=0;form<4;++form)for(const split of [false,true]) {
  const rows=narrowed.filter(row=>row.form===form&&row.split===split&&row.copies>=200);
  assert.deepEqual(rows.map(row=>row.copies),[200,201,400,800]);
  for(let i=1;i<rows.length;++i) {
    assert.ok(rows[i].work<rows[i-1].work*3);
    assert.ok(rows[i].allocated_bytes<rows[i-1].allocated_bytes*3);
  }
}
console.log('narrowed-source 200/201/400/800 admitted cost scaling passed');

// This oracle starts from ordinary JS, independent of the source adapter and
// its unit/type facts. It observes failed conversion, callback mutation and
// successful loop writeback together, including harmless arena changes.
const unitSources=execFileSync(process.argv[3],['--unit-source'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const unitFactories=factories.filter((_,i)=>i>=primitive.length||!Object.is(primitive[i],Infinity)&&!Object.is(primitive[i],-Infinity));
function unitOutcome(fn,factory,mode) {
  const trace=[];let reports=0,callback;
  const input=()=>{trace.push('input');return factory(trace);};
  const normalized=value=>value===failure?'failure':value instanceof Error?value.name:value;
  const report=value=>{
    if(value==='set') {
      trace.push('set');return {[Symbol.toPrimitive](hint){
        trace.push(['cell conversion',hint]);if(mode===2)throw failure;
        if(mode===4){callback();return 1n;}
        return mode===3?Symbol('bad conversion'):0;
      }};
    }
    trace.push(['report',typeof value==='function'?'closure':normalized(value)]);
    if(typeof value==='function'){callback=value;value();}
    if(mode===1&&++reports===1)throw failure;
  };
  try{return {trace,kind:'return',value:fn(input,report)};}
  catch(error){return {trace,kind:'throw',value:normalized(error)};}
}
const unitReferences=[
  function(input,report){let current;try{current=+input();while(current>0){report(current);-current;report(-current);current=current-1;}}catch(error){report(error);}return 0;},
  function(input,report){let current;try{current=+input();while(current>0){report(()=>{current=report('set');});-current;report(-current);current=current-1;}}catch(error){report(error);}return 0;},
  function(input,report){let current;try{current=+input();while(current>0){report(current);-current;report(-current);current--;}}catch(error){report(error);}return 0;},
  function(input,report){let current;try{current=+input();while(current>0){report(()=>{current=report('set');});-current;report(-current);current--;}}catch(error){report(error);}return 0;},
];
let unitComparisons=0,unitMutants=0;
for(const row of unitSources) {
  const generated=Function('return ('+row.source+')')(),reference=unitReferences[+row.captured+2*Number(row.update)];
  for(const factory of unitFactories)for(const mode of [0,1,2,3,4]) {
    assert.deepEqual(unitOutcome(generated,factory,mode),unitOutcome(reference,factory,mode));++unitComparisons;
  }
  const wrapped='('+row.source+')',ast=nodes(parseJavaScript(wrapped));
  assert.equal(ast.filter(node=>node.type==='ExpressionStatement'&&node.expression.type==='UnaryExpression'&&node.expression.operator==='-').length,row.captured?1:0);
  if(row.count!==0)continue;
  const initial=ast.find(node=>node.type==='UnaryExpression'&&node.operator==='+');assert.ok(initial);
  const lostConversion=wrapped.slice(0,initial.start)+wrapped.slice(initial.argument.start,initial.argument.end)+wrapped.slice(initial.end);
  assert.notDeepEqual(unitOutcome(Function('return '+lostConversion)(),()=>1n,0),unitOutcome(reference,()=>1n,0));++unitMutants;
  const target=row.captured?ast.find(node=>node.type==='ExpressionStatement'&&node.expression.type==='UnaryExpression'&&node.expression.operator==='-'):
      row.update?ast.find(node=>node.type==='UpdateExpression'):
      ast.find(node=>node.type==='BinaryExpression'&&node.operator==='-'&&node.right.type==='Literal'&&node.right.value===1);
  assert.ok(target);
  const mutant=wrapped.slice(0,target.start)+(row.captured?';':row.update?'(current -= 2)':'current - 2')+wrapped.slice(target.end);
  assert.notDeepEqual(unitOutcome(Function('return '+mutant)(),()=>3,0),unitOutcome(reference,()=>3,0));++unitMutants;
}
assert.equal(unitSources.length,48);
for(const update of [false,true])for(const captured of [false,true])for(const reverse of [false,true]) {
  const rows=unitSources.filter(row=>row.update===update&&row.captured===captured&&row.reverse===reverse&&row.count>=200);
  assert.deepEqual(rows.map(row=>row.count),[200,201,400,800]);
  for(let i=1;i<rows.length;++i){assert.ok(rows[i].work<rows[i-1].work*3);assert.ok(rows[i].bytes<rows[i-1].bytes*3);}
}
console.log(`${unitSources.length} current-unit loop/capture sources / ${unitComparisons} independent comparisons / ${unitMutants} adverse mutants; 200/201/400/800 cost scaling passed`);

const updates=execFileSync(process.argv[3],['--updates'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const updateFactories=[...factories,()=>Number.MAX_VALUE,()=>-Number.MAX_VALUE,()=>Number.MAX_SAFE_INTEGER,()=>2n];
function updateOutcome(fn,factory,throws) {
  const trace=[];let inputValue;
  const input=()=>{trace.push('input');return inputValue=factory(trace);};
  const normalize=value=>value===failure?'failure':value instanceof Error?value.name:
    value===inputValue&&value!==null&&typeof value==='object'?'input':value;
  const report=(...values)=>{trace.push(['report',...values.map(normalize)]);if(throws)throw failure;};
  try{return {trace,kind:'return',value:normalize(fn(input,report))};}
  catch(error){return {trace,kind:'throw',value:normalize(error)};}
}
let updateComparisons=0,updateMutants=0;
for(const row of updates) {
  const reference=function(input,report) {
    let saved=7;saved=row.numeric?+input():input();
    const body=()=>{
      let result;
      if(row.increment)result=row.prefix?++saved:saved++;
      else result=row.prefix?--saved:saved--;
      report(result,saved);-saved;
    };
    if(row.caught){try{body();}catch(error){report(saved,error);}}else body();
    return saved;
  };
  const generated=Function('return ('+row.source+')')();
  for(const factory of updateFactories)for(const throws of [false,true]) {
    assert.deepEqual(updateOutcome(generated,factory,throws),updateOutcome(reference,factory,throws));++updateComparisons;
  }
  const source='('+row.source+')',ast=nodes(parseJavaScript(source));
  assert.equal(ast.filter(node=>node.type==='ExpressionStatement'&&node.expression.type==='UnaryExpression'&&node.expression.operator==='-').length,row.numeric?0:1);
  const update=ast.find(node=>node.type==='UpdateExpression');assert.ok(update);
  const argument=source.slice(update.argument.start,update.argument.end);
  const operator=row.increment?'++':'--',opposite=row.increment?'--':'++';
  const changes=[row.prefix?`(${argument}${operator})`:`(${operator}${argument})`,
    row.prefix?`(${opposite}${argument})`:`(${argument}${opposite})`,argument];
  for(const replacement of changes) {
    const mutant=source.slice(0,update.start)+replacement+source.slice(update.end);
    assert.notDeepEqual(updateOutcome(Function('return '+mutant)(),()=>2,false),updateOutcome(reference,()=>2,false));++updateMutants;
  }
}
assert.equal(updates.length,32);
console.log(`${updates.length} captured-update sources / ${updateComparisons} independent comparisons / ${updateMutants} adverse mutants`);

// Publication-order regression: value-used and discarded comma operands share
// one completed projection. Compare execution separately from arena numbering.
const publication=execFileSync(process.argv[3],['--publication'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const publicationFailure=Symbol('publication failure');
const publicationValues=[undefined,null,0,-0,NaN,7n,Symbol('value'),{}];
function publicationOutcome(fn,value,failAt) {
  const trace=[];let calls=0;
  const event=(...args)=>{trace.push(args);if(++calls===failAt)throw publicationFailure;};
  try {return {trace,value:fn(()=>{event('input');return value;},x=>event('report',x))};}
  catch(error){return {trace,error:error===publicationFailure?error:error.name};}
}
function publicationReference(row) {
  return Function('input','report',
    (row.before?'':'let saved=input();')+
    'for(let i=0;i<'+row.count+';++i)report((saved,input()));(report(saved),7);'+
    (row.before?'let saved=input();':'')+'return saved;');
}
let publicationComparisons=0,publicationMutants=0;
for(const row of publication) {
  const wrapped='('+row.source+')',all=nodes(parseJavaScript(wrapped));
  const discardedRead=n=>n.type==='SequenceExpression'&&n.expressions[0].type==='Identifier'&&n.expressions[0].name==='saved';
  if(!row.before)assert.equal(all.filter(discardedRead).length,0);
  const actual=Function('return '+row.source)(),expected=publicationReference(row);
  for(const value of publicationValues)for(const failAt of [0,1,2,row.count+1,2*row.count+2]) {
    assert.deepEqual(publicationOutcome(actual,value,failAt),publicationOutcome(expected,value,failAt));++publicationComparisons;
  }
  if(row.count===1) {
    const target=row.before?all.find(discardedRead):all.find(n=>{
      if(n.type!=='CallExpression')return false;
      const callee=n.callee.type==='SequenceExpression'?n.callee.expressions.at(-1):n.callee;
      return callee.type==='Identifier'&&callee.name==='input';
    });
    assert.ok(target);
    const replacement=row.before?wrapped.slice(target.expressions[1].start,target.expressions[1].end):'undefined';
    const mutant=Function('return '+wrapped.slice(0,target.start)+replacement+wrapped.slice(target.end))();
    assert.notDeepEqual(publicationOutcome(mutant,42,0),publicationOutcome(expected,42,0));++publicationMutants;
  }
}
assert.equal(publication.length,24);
for(const before of [false,true])for(const reverse of [false,true]) {
  const growth=publication.filter(row=>row.before===before&&row.reverse===reverse&&row.count>=200);
  for(let i=1;i<growth.length;++i){assert.ok(growth[i].work<growth[i-1].work*3);assert.ok(growth[i].bytes<growth[i-1].bytes*3);}
}
console.log(`${publication.length} joint-projection sources / ${publicationComparisons} comparisons / ${publicationMutants} adverse mutants`);
