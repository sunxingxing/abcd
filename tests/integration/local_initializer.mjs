import assert from 'node:assert/strict';
import {generatedSource as execFileSync} from '../support/source/generated_fixture.mjs';
import {parseJavaScript} from './class_reference.mjs';

const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:16_000_000}).trim().split('\n').map(JSON.parse);
const token={},second={},failure=Symbol('original throw');
const isAsync=form=>form===11||form===12,isGenerator=form=>form===13||form===14;
function reference(row) {
  const {form,count}=row,closure=form===4?'((value,write)=>{if(write)saved=value;return saved;})':'(()=>saved)';
  const captured=![0,7,8,9,10].includes(form),before=[2,12,14].includes(form);
  let body=(before?'report('+closure+');':'')+(form===7?'report(saved);':'')+'report(0);'.repeat(count);
  body+='saved='+(isAsync(form)?'await ':isGenerator(form)?'yield ':'')+'seed('+(form===3?closure:form===8?'saved':'')+');';
  body+=captured&&!before&&form!==3?(form===15?'{report('+closure+');}':'report('+closure+');'):'report(saved);';
  if(form===5||form===6)body=(form===5?'let saved;':'')+'while(again()){'+(form===6?'let saved;':'')+body+'}return;';
  else if(form===9)body='let saved;{'+body+'return saved;}';
  else if(form===10)body='let saved;try{'+body+'return saved;}finally{report(saved);}';
  else body='let saved;'+body+'return saved;';
  return Function('"use strict";return ('+(isAsync(form)?'async function':isGenerator(form)?'function*':'function')+'(seed,report,again){'+body+'});')();
}
async function observe(fn,row,options={}) {
  const {failAt,reject=false,request='next',iterations=3}=options;
  const value=Object.hasOwn(options,'value')?options.value:token;
  const trace=[],closures=[],requests=[];let events=0,seeds=0,checks=0,returned,thrown,didThrow=false;
  const event=(kind,value)=>{trace.push([kind,value]);if(++events===failAt)throw failure;};
  const read=(fn,write=false)=>{
    try{return ['value',fn(second,write)];}catch(error){if(error instanceof assert.AssertionError)throw error;return ['error',error===failure?error:error.name];}
  };
  const retain=fn=>{closures.push(fn);trace.push(['capture',read(fn)]);};
  const seed=function(fn) {
    assert.equal(this,undefined);if(typeof fn==='function')retain(fn);else if(arguments.length)trace.push(['argument',fn]);
    const result=seeds++?second:value;event('seed',result);
    if(isAsync(row.form)){queueMicrotask(()=>trace.push(['job',closures.map(fn=>read(fn))]));return reject?Promise.reject(failure):Promise.resolve(result);}
    return result;
  };
  const report=function(value){assert.equal(this,undefined);if(typeof value==='function'){retain(value);event('report-function',closures.length);if(row.form===4)trace.push(['write',read(value,true)]);}else event('report',value);};
  const again=function(){assert.equal(this,undefined);event('again',checks);return checks++<iterations;};
  try {
    returned=fn(seed,report,again);
    if(isGenerator(row.form)) {
      const generator=returned,first=generator.next();requests.push(first);trace.push(['suspended',closures.map(fn=>read(fn))]);
      const end=request==='throw'?generator.throw(failure):request==='return'?generator.return(second):generator.next(value);
      assert.equal(end.done,true);requests.push(end);returned=end.value;
    }
    if(isAsync(row.form))returned=await returned;
  }catch(error){if(error instanceof assert.AssertionError)throw error;didThrow=true;thrown=error===failure?error:error.name;returned=undefined;}
  const after=closures.map(fn=>read(fn));
  if(row.form===4)for(const fn of closures)after.push(read(fn,true));
  return {trace,requests,returned,thrown,didThrow,after,events};
}
function compare(actual,expected) {
  assert.equal(actual.didThrow,expected.didThrow);assert.ok(Object.is(actual.returned,expected.returned));
  assert.ok(Object.is(actual.thrown,expected.thrown));
  const exact=(left,right)=>{
    if(left===token||right===token||left===second||right===second||left===null||right===null||typeof left!=='object'||typeof right!=='object') {
      assert.ok(Object.is(left,right));return;
    }
    assert.deepEqual(Object.keys(left),Object.keys(right));
    for(const key of Object.keys(left))exact(left[key],right[key]);
  };
  exact(actual,expected);
}
const compile=source=>Function('"use strict";return ('+source+');')();
function walk(node,visit) {
  if(!node||typeof node!=='object')return;
  if(node.type)visit(node);
  for(const value of Object.values(node)) {
    if(Array.isArray(value)){for(const item of value)walk(item,visit);}
    else if(value&&typeof value==='object')walk(value,visit);
  }
}
let comparisons=0,mutations=0;
for(const row of rows) {
  const ast=parseJavaScript('('+row.source+')');let declaration;
  walk(ast,node=>{if(node.type==='VariableDeclaration'&&node.declarations.some(value=>value.id.name==='saved'))declaration=node;});
  assert.ok(declaration);assert.equal(declaration.kind,'let');assert.equal(Boolean(declaration.declarations[0].init),row.fold);
  const actual=compile(row.source),expected=reference(row);
  for(const value of [undefined,null,false,0,-0,NaN,1n,Symbol.for('value'),token]) {
    const baseline=await observe(expected,row,{value});
    const faults=row.count<=1?Array.from({length:baseline.events+1},(_,i)=>i):[0,1,row.count+1,baseline.events];
    for(const failAt of faults){const options={value,failAt};compare(await observe(actual,row,options),await observe(expected,row,options));++comparisons;}
  }
  if(row.form===5||row.form===6)for(const iterations of [0,1,4]){compare(await observe(actual,row,{iterations}),await observe(expected,row,{iterations}));++comparisons;}
  if(isAsync(row.form)){compare(await observe(actual,row,{reject:true}),await observe(expected,row,{reject:true}));++comparisons;}
  if(isGenerator(row.form))for(const request of ['throw','return']){compare(await observe(actual,row,{request}),await observe(expected,row,{request}));++comparisons;}
  if(row.count!==1||row.reverse)continue;
  const changes=[];
  if(row.fold)changes.push(source=>source.replace(/let saved\s*=/,'const saved ='));
  if([2,3,5,7,8,10,12,14].includes(row.form))changes.push(source=>source.replace('let saved;','').replace(/\(saved = ([^;]+)\);/,'let saved = $1;'));
  // Mutable captures distinguish let from const; other positive mutations
  // change initialization count/value and are independently valid JS.
  if(row.fold&&row.form!==4)changes[0]=source=>source.replace(/let saved\s*=\s*([^;]+);/,'let saved = (seed(), $1);');
  for(const change of changes) {
    const changed=change(row.source);assert.notEqual(changed,row.source);parseJavaScript('('+changed+')');
    const mutant=compile(changed);let detected=false;
    for(const options of [{},{failAt:row.count+1},{reject:true},{request:'throw'}]) {
      try{compare(await observe(mutant,row,options),await observe(expected,row,options));}catch(error){if(!(error instanceof assert.AssertionError))throw error;detected=true;break;}
    }
    assert.equal(detected,true,'undetected valid mutation form '+row.form);++mutations;
  }
}
console.log(JSON.stringify({sources:rows.length,comparisons,mutations,platformExecution:false}));
