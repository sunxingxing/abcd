import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const failure=Symbol('throw'),token={},second={};
function reference(row) {
  const {form,native}=row;
  const initialize=(name,value,constant=false)=>native||constant?`${constant?'const':'let'} ${name}=${value};`:`let ${name};${name}=${value};`;
  let body;
  if(form===8)body='let current=current;let scratch;';
  else {
    const value=form===0?'42':[3,4,5,7,12].includes(form)?'seed()':form===11?'{}':form===2?'(report(input),input)':'input';
    body=initialize('current',value)+(form===7?'report(scratch);':form===15?'scratch=seed();':'')+
      ([3,4,11,12].includes(form)?'let scratch;':initialize('scratch',form===16?'(report(input),42)':'current',form===6));
  }
  if(form===5||form===15)body+='++scratch;current=scratch;';
  if(form===13)body+='if(flag){++scratch;current=scratch;}';
  if(form===14)body+='while(flag()){++scratch;current=scratch;}';
  if(form===16)body+='report(scratch);current=scratch;';
  if(form===10)body+='try{scratch=seed();current=scratch;}catch(error){report(current);}';
  const result=[3,4,5,8,10,11,12,13,14,15,16].includes(form)?'current':'scratch';
  const tail=(form===3?'':`report(${result});`)+`return ${result};`;
  body+=form===9?`{let input=17;${tail}}`:tail;
  return Function(`return function(input,seed,report,flag){'use strict';${body}}`)();
}
function observe(fn,row,input,fault,throwSeed,coercion) {
  const trace=[];let at=0,returned,thrown,object,iterations=0;
  const event=(kind,value)=>{trace.push([kind,value]);if(++at===fault)throw failure;};
  const numeric={ [Symbol.toPrimitive](hint){event('numeric',hint);if(coercion)throw failure;return 3;} };
  const seed=function(){assert.equal(this,undefined);event('seed',input);if(throwSeed)throw failure;return row.form===5?numeric:input;};
  const report=function(value){assert.equal(this,undefined);
    if(row.form===11){object??=value;assert.equal(value,object);value.visits=(value.visits??0)+1;event('report',value.visits);}
    else event('report',value);
  };
  const flag=row.form===14?()=>{event('iteration',iterations);return iterations++<2;}:!coercion;
  try{ returned=fn(input,seed,report,flag);if(row.form===11){assert.equal(returned,object);returned=token;} }
  catch(error){thrown=error;}
  if(thrown instanceof ReferenceError)thrown='ReferenceError';
  return {trace,returned,thrown};
}
let comparisons=0,mutations=0;
const observed=(source,row,...args)=>observe(Function(`return ${source}`)(),row,...args);
for(const row of rows) {
  parseJavaScript(row.source);
  const expected=reference(row),actual=Function(`return ${row.source}`)();
  for(const input of [undefined,null,false,0,-0,NaN,42,'text',token,second])
    for(const fault of [0,1,2,3,4])for(const throwSeed of [false,true])for(const coercion of [false,true]) {
      assert.deepEqual(observe(actual,row,input,fault,throwSeed,coercion),observe(expected,row,input,fault,throwSeed,coercion),JSON.stringify(row));++comparisons;
    }
  if(row.form<=3)assert.doesNotMatch(row.source,/\b(?:current|scratch)\b/);
  if(row.form===5) {
    const ast=parseJavaScript(row.source);const declarations=[];
    const walk=node=>{if(!node||typeof node!=='object')return;if(node.type==='VariableDeclaration')declarations.push(...node.declarations);for(const value of Object.values(node))if(Array.isArray(value))value.forEach(walk);else if(value&&typeof value==='object')walk(value);};
    walk(ast);assert.equal(declarations.length,1,'all successful writes share one live slot');
  }
  if(row.count===0&&!row.reverse&&row.native) {
    if(row.form===2) {
      assert.notDeepEqual(observed('(function(input){return input;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===5) {
      assert.notDeepEqual(observed('(function(input,seed,report){let value=seed();report(value);return value;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===7||row.form===8||row.form===15) {
      assert.notDeepEqual(observed('(function(input){return input;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===16) {
      assert.notDeepEqual(observed('(function(input,seed,report){report(input);let value=void 0;report(value);report(value);return value;})',row,token,0,false,false),observe(expected,row,token,0,false,false));++mutations;
    }
    if(row.form===10) {
      assert.notDeepEqual(observed('(function(input,seed,report){let value;try{value=seed();}catch(e){report(value);}report(value);return value;})',row,token,0,true,false),observe(expected,row,token,0,true,false));++mutations;
    }
  }
}
console.log(JSON.stringify({sources:rows.length,comparisons,mutations}));
