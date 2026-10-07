import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const token={},resumed={},failure=Symbol('original failure');
function reference({form}) {
  const kind=form===8?'async function':form===9?'function*':'function';
  const value=form===8?'await input.read()':form===9?'yield input.read()':form===10?'__snapshotMissing':form===5?'(flag=input.read())':'input.read()';
  let body='let saved;saved='+value+';';
  if(form===1)body+='if(saved)return true;else return false;';
  else if(form===2)body='let saved;while(true){saved=input.read();if(!saved)break;report();}return false;';
  else if(form===3)body+='report(saved);return saved;';
  else if(form===4)body+='input.effect();return saved;';
  else if(form===5)body+='report(flag,saved);return false;';
  else if(form===7)body+='if(true)return false;return saved;';
  else if(form===10)body+='return typeof saved;';
  else if(form===12)body+='{return saved;let input=false;}';
  else if(form===13)body+='if(false)return saved;';
  else if(form===14)body='let saved;try{saved=input.read();return saved;}catch(error){return error;}';
  else if(form===15)body='let saved;try{saved=input.read();}catch(error){return error;}return saved;';
  else if(form===16)body='let saved;try{saved=input.read();return saved;}catch(error){report(saved);return error;}';
  else if(form===17)body='let local,saved;{local=input.read();saved=!local.value;}return saved;';
  else body+='return saved;';
  return Function('"use strict";return ('+kind+'(input,report,flag){'+body+'});')();
}
async function observe(fn,row,options={}) {
  const value=Object.hasOwn(options,'value')?options.value:token;
  const thrownValue=Object.hasOwn(options,'failureValue')?options.failureValue:failure;
  const trace=[];let events=0,reads=0,result,error,didThrow=false;
  const encode=v=>{
    if(v===null||typeof v!=='object'&&typeof v!=='function')return v;
    if(v===token)return {identity:'token'};if(v===resumed)return {identity:'resume'};
    if(v===value)return {identity:'input-value'};if(v===thrownValue)return {identity:'failure'};
    if(v instanceof Error)return {error:v.name};assert.fail('unknown object identity');
  };
  const event=(...entry)=>{trace.push(entry);if(++events===options.failAt)throw thrownValue;};
  const input={
    get read(){event('get-read');return function(){assert.equal(this,input);event('call-read');++reads;
      if(row.form===2)return reads<4?value:false;
      if(options.thenable)return {then(resolve,reject){event('then');if(options.reject)reject(thrownValue);else resolve(value);}};
      return value;
    };},
    get effect(){event('get-effect');return function(){assert.equal(this,input);event('call-effect');};},
  };
  const report=function(...args){assert.equal(this,undefined);event('report',...args.map(encode));};
  try {
    let returned=fn(input,report,options.flag??false);
    if(row.form===9) {
      let next=returned.next();event('yield',encode(next.value),next.done);
      next=options.request==='throw'?returned.throw(thrownValue):options.request==='return'?returned.return(resumed):returned.next(resumed);
      event('resume',encode(next.value),next.done);result=next.value;
    } else result=await returned;
  } catch(caught){if(caught instanceof assert.AssertionError)throw caught;didThrow=true;error=encode(caught);}
  return {trace,result:encode(result),didThrow,error};
}
let cases=0,mutations=0;
for(const row of rows) {
  const pending=[parseJavaScript(row.source)];let snapshots=0;
  while(pending.length){const node=pending.pop();if(node.type==='Identifier'&&node.name==='saved')++snapshots;
    for(const child of Object.values(node)){if(Array.isArray(child)){for(const item of child)if(item?.type)pending.push(item);}else if(child?.type)pending.push(child);}}
  assert.equal(snapshots===0,row.folded,`snapshot representation form=${row.form}`);
  const actual=Function('"use strict";return '+row.source)(),expected=reference(row);
  const scenarios=[...([undefined,null,false,0,-0,NaN,1n,Symbol.for('value'),token,'ok'].map(value=>({value})))];
  for(const failureValue of [undefined,null,false,0,NaN,failure,token])for(const failAt of [1,2,3,4])scenarios.push({failureValue,failAt});
  if(row.form===8)scenarios.push({thenable:true},{thenable:true,reject:true},{thenable:true,failAt:3});
  if(row.form===9)scenarios.push({request:'throw'},{request:'return'});
  for(const scenario of scenarios){assert.deepStrictEqual(await observe(actual,row,scenario),await observe(expected,row,scenario),`form=${row.form} count=${row.count}`);++cases;}
  if(!row.count&&[0,2,5,10].includes(row.form)) {
    let wrong;
    if(row.form===0)wrong=row.source.replace('input.read()','(input.read(), input.read())');
    if(row.form===2)wrong=row.source.replace('input.read()','false');
    if(row.form===5)wrong=row.source.replace(/\(flag = input\.read\(\)\)/,'false').replace(/\(0, report\)\(flag, saved\)/,'(0, report)(flag, (flag = input.read()))');
    if(row.form===10)wrong=row.source.replace(/\(0, __snapshotMissing\)/,'__snapshotMissing');
    assert.notEqual(wrong,row.source);parseJavaScript(wrong);
    assert.notDeepStrictEqual(await observe(Function('"use strict";return '+wrong)(),row),await observe(expected,row));++mutations;
  }
}
console.log(`statement-snapshot: ${rows.length} generated functions, ${cases} behavior cases, ${mutations} detected mutations`);
