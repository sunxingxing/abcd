import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';

const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const returnedObject={},failure=Symbol('original failure'),resumeObject={};
// Independent language programs; the production tree/source is never used to
// derive expected effects, values, binding writes, or suspension behavior.
const rhs=[
  'state','(state=other())','++state','--state','state++','(state=input,++state)',
  '(flag?++state:(state=other()))','(flag?++state:other())',
  '(state&&(state=other()))','(state||(state=other()))','(state??(state=other()))',
  '(++state,other(state))','input.value','state','await (state=other())',
  'yield (state=other())','state','(state=(state=input))','(state=await other())','(state=yield other())',
];
function reference({form,count}) {
  const expression='('+('report(flag),'.repeat(count))+rhs[form]+')';
  const statement='report('+(form===13?'input.value':'state')+'='+expression+');';
  const body=form===16?statement+'let state=input;':'let state=input;'+statement;
  return Function('"use strict";return ('+([14,18].includes(form)?'async function':[15,19].includes(form)?'function*':'function')+
    '(input,report,other,flag){'+body+'return state;});')();
}
async function observe(fn,row,options={}) {
  const {kind='number',flag=true,failAt=-1,request='next',otherValue=returnedObject}=options;
  const trace=[];let events=0,returned,thrown,didThrow=false;
  const throwValue=Object.hasOwn(options,'failureValue')?options.failureValue:failure;
  const event=(...entry)=>{trace.push(entry);if(++events===failAt)throw throwValue;};
  const input=kind==='number'?7:kind==='bigint'?7n:kind==='zero'?0:kind==='negativeZero'?-0:
    kind==='nan'?NaN:kind==='null'?null:kind==='undefined'?undefined:kind==='string'?'7':kind==='symbol'?Symbol.for('input'):{
      [Symbol.toPrimitive](hint){event('coerce',hint);return kind==='coerceBigint'?7n:7;},
      get value(){event('get');return returnedObject;},
      set value(value){event('set',encode(value));},
    };
  const encode=value=>{
    if(value===null||typeof value!=='object'&&typeof value!=='function')return value;
    if(value===input)return {identity:'input'};
    if(value===otherValue)return {identity:'other'};
    if(value===resumeObject)return {identity:'resume'};
    assert.fail('unexpected object identity');
  };
  const report=value=>event('report',encode(value));
  const other=(...args)=>{event('other',...args.map(encode));return options.thenable?{then(resolve,reject){event('then');if(options.reject)reject(failure);else resolve(otherValue);}}:otherValue;};
  try {
    let result=fn(input,report,other,flag);
    if([15,19].includes(row.form)) {
      let next=result.next();event('yield',encode(next.value),next.done);
      if(request==='throw')next=result.throw(failure);
      else if(request==='return')next=result.return(resumeObject);
      else next=result.next(resumeObject);
      event('resume',encode(next.value),next.done);returned=next.value;
    } else returned=await result;
  } catch(error) {
    if(error instanceof assert.AssertionError)throw error;
    didThrow=true;thrown=Object.is(error,throwValue)?encode(throwValue):error===failure?failure:error instanceof Error?error.name:encode(error);
  }
  return {trace,returned:encode(returned),didThrow,thrown};
}
let cases=0,mutations=0;
for(const row of rows) {
  const pending=[parseJavaScript(row.source)];let assignments=0;
  while(pending.length) {
    const node=pending.pop();if(node.type==='AssignmentExpression')++assignments;
    for(const value of Object.values(node)) {
      if(Array.isArray(value))for(const child of value){if(child&&typeof child.type==='string')pending.push(child);}
      else if(value&&typeof value.type==='string')pending.push(value);
    }
  }
  // In state = await/yield (state = value), the private inner write is dead:
  // normal resume overwrites it, abrupt resume cannot expose this uncaptured
  // binding. The suspension and its value remain and are executed below.
  const count=[0,1,0,0,1,1,1,1,1,1,1,1,1,1,2,2,0,1,1,1][row.form];
  const deadInner=row.temporary&&[14,15].includes(row.form);
  assert.equal(assignments,count-Number(deadInner),`stored-result syntax form=${row.form}`);
  const actual=Function('"use strict";return '+row.source)(),expected=reference(row);
  const scenarios=[];
  for(const kind of ['number','bigint','zero','negativeZero','nan','null','undefined','string','symbol','coerceNumber','coerceBigint'])
    for(const flag of [false,true])scenarios.push({kind,flag});
  scenarios.push({failAt:1},{failAt:row.count+1},{failAt:row.count+2},
    {otherValue:undefined},{otherValue:0},{otherValue:7n});
  for(const failureValue of [undefined,null,false,0,NaN,returnedObject])scenarios.push({failAt:row.count+1,failureValue});
  if([14,18].includes(row.form))scenarios.push({thenable:true},{thenable:true,reject:true},{thenable:true,failAt:row.count+2});
  if([15,19].includes(row.form))scenarios.push({request:'throw'},{request:'return'});
  for(const scenario of scenarios) {
    assert.deepStrictEqual(await observe(actual,row,scenario),await observe(expected,row,scenario),JSON.stringify({form:row.form,count:row.count,scenario},(_,value)=>typeof value==='bigint'?String(value):value));++cases;
  }
  if(!row.count&&[1,2,13].includes(row.form)) {
    const wrong=row.form===1?row.source.replace('(0, other)()', 'input'):
      row.form===2?row.source.replace('++state','state++'):
      row.source.replace('input.value = state','input.value = 0');
    assert.notEqual(wrong,row.source);parseJavaScript(wrong);
    const changed=Function('"use strict";return '+wrong)();
    assert.notDeepStrictEqual(await observe(changed,row,{kind:'coerceNumber'}),await observe(expected,row,{kind:'coerceNumber'}));++mutations;
  }
}
console.log(`stored-result: ${rows.length} generated functions, ${cases} behavior cases, ${mutations} detected mutations`);
