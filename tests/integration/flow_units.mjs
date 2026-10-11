import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {isDeepStrictEqual} from 'node:util';
import {parseJavaScript} from './class_reference.mjs';

const rows=execFileSync(process.argv[2],['--unit-loops'],{encoding:'utf8',maxBuffer:16_000_000})
  .trim().split('\n').map(JSON.parse);
const inputFailure=Symbol('input failure'),reportFailure=Symbol('report failure'),nextFailure=Symbol('next failure');
const values=[undefined,null,false,0,-0,NaN,7,8n,'value',Symbol('value'),{}];
function reference(next,input,report) {
  while(next()) {
    let cell=true;
    try {cell=input();} catch(error) {report(error);}
    report(cell);
  }
}
function outcome(fn,value,rounds,mask,failReport,failNext) {
  const trace=[];let calls=0,reports=0,iterations=0;
  const next=()=>{trace.push(['next',iterations]);if(++calls===failNext)throw nextFailure;return iterations++<rounds;};
  const input=()=>{trace.push(['input',iterations]);if(mask&(1<<(iterations-1)))throw inputFailure;return value;};
  const report=result=>{trace.push(['report',result]);if(++reports===failReport)throw reportFailure;};
  try {return {trace,value:fn(next,input,report)};}catch(error){return {trace,error};}
}
function nodes(root) {
  const result=[],pending=[root];
  while(pending.length) {
    const node=pending.pop();if(!node||typeof node!=='object')continue;
    if(node.type)result.push(node);
    for(const value of Object.values(node))if(value&&typeof value==='object')
      Array.isArray(value)?pending.push(...value):pending.push(value);
  }
  return result;
}
let comparisons=0,mutants=0;
for(const row of rows) {
  const source='('+row.source+')',actual=Function('return '+source)();
  for(const value of values)for(const rounds of [0,1,2,4])for(const mask of [0,1,2,5,15])
    for(const failReport of [0,1,3])for(const failNext of [0,1,3]) {
      assert.deepEqual(outcome(actual,value,rounds,mask,failReport,failNext),
        outcome(reference,value,rounds,mask,failReport,failNext));++comparisons;
    }
  const ast=nodes(parseJavaScript(source));
  const loop=ast.find(node=>node.type==='WhileStatement');assert.ok(loop);
  const declaration=ast.find(node=>node.type==='VariableDeclaration'&&node.declarations.some(d=>d.init?.value===true));
  assert.ok(declaration&&declaration.start>loop.start,'per-entry initializer must remain in the loop');
  const binding=declaration.declarations.find(d=>d.init?.value===true).id.name;
  const assignment=ast.find(node=>node.type==='AssignmentExpression'&&node.left.name===binding);assert.ok(assignment);
  const caught=ast.find(node=>node.type==='CatchClause');assert.ok(caught);
  const caughtRead=nodes(caught.body).find(node=>node.type==='Identifier'&&node.name===caught.param.name);assert.ok(caughtRead);
  // Independent adverse source transformations: reuse last iteration's value,
  // publish a write before a throwing call, or read the wrong catch value.
  const hoisted=source.slice(0,loop.start)+source.slice(declaration.start,declaration.end)+
    source.slice(loop.start,declaration.start)+source.slice(declaration.end);
  const premature=source.slice(0,assignment.start)+`(${binding}=false, `+
    source.slice(assignment.start,assignment.end)+')'+source.slice(assignment.end);
  const wrongCatch=source.slice(0,caughtRead.start)+binding+source.slice(caughtRead.end);
  for(const text of [hoisted,premature,wrongCatch]) {
    const mutant=Function('return '+text)();
    assert.ok([1,2,5].some(mask=>!isDeepStrictEqual(outcome(mutant,7,4,mask,0,0),outcome(reference,7,4,mask,0,0))));
    ++mutants;
  }
}
assert.equal(rows.length,24);
console.log(`${rows.length} decoded loop/catch sources / ${comparisons} independent comparisons / ${mutants} adverse mutants`);
