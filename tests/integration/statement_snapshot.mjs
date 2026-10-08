import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8',maxBuffer:32_000_000}).trim().split('\n').map(JSON.parse);
const token={},resumed={},failure=Symbol('original failure');
function reference({form,count}) {
  const kind=form===8?'async function':form===9?'function*':'function';
  const value=form===8?'await input.read()':form===9?'yield input.read()':form===10?'__snapshotMissing':(form===39||form===40)?'!!Math':form===41?'!!__snapshotFree':form===44?'this':(form===42||form===43)?'this.read()':(form===5||form===37)?'(flag=input.read())':'input.read()';
  let body='let saved;saved='+value+';';
  if(form===1)body+='if(saved)return true;else return false;';
  else if(form===2)body='let saved;while(true){saved=input.read();if(!saved)break;report();}return false;';
  else if(form===3)body+='report(saved);return saved;';
  else if(form===4)body+='input.effect();return saved;';
  else if(form===5)body+='report(flag,saved);return false;';
  else if(form===7)body+='if(true)return false;return saved;';
  else if(form===10)body+='return typeof saved;';
  else if(form===12)body+='{return saved;let input=false;}';
  else if(form===36||form===37||form===38||form===40||form===41||form===42||form===44)body+='{return saved;let flag=false;}';
  else if(form===43)body+='{return saved;let input=false;}';
  else if(form===39)body+='{return saved;let Math=false;}';
  else if(form===13)body+='if(false)return saved;';
  else if(form===14)body='let saved;try{saved=input.read();return saved;}catch(error){return error;}';
  else if(form===15)body='let saved;try{saved=input.read();}catch(error){return error;}return saved;';
  else if(form===16)body='let saved;try{saved=input.read();return saved;}catch(error){report(saved);return error;}';
  else if(form===17)body='let local,saved;{local=input.read();saved=!local.value;}return saved;';
  else if(form===19||form===20)body='let saved;'+(form===19?'saved;':'report(saved);')+'saved=input.read();return saved;';
  else if(form===21||form===22)body+='return ('+(form===21?'flag,':'input.effect(),').repeat(count+1)+'saved);';
  else if(form===23)body+='if(false){}else{}return saved;';
  else if(form===24)body+='debugger;return saved;';
  else if(form===25)body+='while(true){return saved;}return false;';
  else if(form===26)body+='while(true){if(saved)break;report();if(flag)break;flag=true;}return false;';
  else if(form===27||form===28)body+=(form===27?'saved=saved;':'saved=(input.effect(),saved);').repeat(count+1)+'return saved;';
  else if(form>=29&&form<=31)body='let saved;saved;'+(form===30?'saved=input.effect();':'saved=flag;').repeat(count+1)+(form===31?'report(saved);':'')+'saved=input.read();return saved;';
  else if(form>=32&&form<=34)body='let saved;('+ (form===33?'saved=input.read()'+',false'.repeat(count+1):(form===34?'input.effect(),':'false,').repeat(count+1)+'saved=input.read()')+');return saved;';
  else if(form===35)body='saved=input.read();return saved;let saved;';
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
    if(v===input)return {identity:'receiver'};if(v===other)return {identity:'other-receiver'};
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
  const other={get read(){event('get-other');return function(){assert.equal(this,other);event('call-other');return resumed;};}};
  const receiver=(row.form>=42&&row.form<=44)?(options.otherReceiver?other:Object.hasOwn(options,'receiver')?options.receiver:input):undefined;
  const oldGlobal=Object.getOwnPropertyDescriptor(globalThis,'__snapshotFree');
  if(row.form===41)Object.defineProperty(globalThis,'__snapshotFree',{configurable:true,get(){event('global-read');return value;}});
  try {
    let returned;
    if(options.beforeSuper) {
      // Execute the generated body in a real derived-constructor this TDZ.
      // This test wrapper supplies no source to the decompiler.
      const text='('+fn.toString()+')',body=parseJavaScript(text).body[0].expression.body;
      const Constructor=Function('return class extends class {} {constructor(input,report,flag) '+text.slice(body.start,body.end)+'}')();
      returned=new Constructor(input,report,options.flag??false);
    } else returned=fn.call(receiver,input,report,options.flag??false);
    if(row.form===9) {
      let next=returned.next();event('yield',encode(next.value),next.done);
      next=options.request==='throw'?returned.throw(thrownValue):options.request==='return'?returned.return(resumed):returned.next(resumed);
      event('resume',encode(next.value),next.done);result=next.value;
    } else result=await returned;
  } catch(caught){if(caught instanceof assert.AssertionError)throw caught;didThrow=true;error=encode(caught);}
  finally {if(row.form===41){if(oldGlobal)Object.defineProperty(globalThis,'__snapshotFree',oldGlobal);else delete globalThis.__snapshotFree;}}
  return {trace,result:encode(result),didThrow,error};
}
let cases=0,mutations=0;
for(const row of rows) {
  const pending=[parseJavaScript(row.source)];let snapshots=0;
  while(pending.length){const node=pending.pop();if(node.type==='Identifier'&&node.name==='saved')++snapshots;
    for(const child of Object.values(node)){if(Array.isArray(child)){for(const item of child)if(item?.type)pending.push(item);}else if(child?.type)pending.push(child);}}
  assert.equal(snapshots===0,row.folded||row.aliased,`snapshot representation form=${row.form}`);
  const actual=Function('"use strict";return '+row.source)(),expected=reference(row);
  const scenarios=[...([undefined,null,false,0,-0,NaN,1n,Symbol.for('value'),token,'ok'].map(value=>({value})))];
  for(const failureValue of [undefined,null,false,0,NaN,failure,token])for(const failAt of [1,2,3,4])scenarios.push({failureValue,failAt});
  if(row.form>=42&&row.form<=44)scenarios.push({receiver:null},{receiver:undefined},{receiver:7},{otherReceiver:true},{beforeSuper:true});
  if(row.form===8)scenarios.push({thenable:true},{thenable:true,reject:true},{thenable:true,failAt:3});
  if(row.form===9)scenarios.push({request:'throw'},{request:'return'});
  for(const scenario of scenarios){assert.deepStrictEqual(await observe(actual,row,scenario),await observe(expected,row,scenario),`form=${row.form} count=${row.count}`);++cases;}
  if(!row.count&&row.form===35) {
    const wrong=Function('input','report','flag','"use strict";return input.read();');
    assert.notDeepStrictEqual(await observe(wrong,row),await observe(expected,row));++mutations;
  }
  if(!row.count&&(row.form===30||row.form===31||row.form===34)) {
    const bodies=['return input.read();'];
    if(row.form===30||row.form===34)bodies.push('const value=input.read();input.effect();return value;');
    for(const body of bodies) {
      const wrong=Function('input','report','flag','"use strict";'+body);
      assert.notDeepStrictEqual(await observe(wrong,row),await observe(expected,row));++mutations;
    }
  }
  if(!row.count&&row.form===28) {
    for(const body of ['return input.read();','input.effect();return input.read();']) {
      const wrong=Function('input','report','flag','"use strict";'+body);
      assert.notDeepStrictEqual(await observe(wrong,row),await observe(expected,row));++mutations;
    }
  }
  if(!row.count&&row.form===26) {
    const wrong=Function('input','report','flag','"use strict";input.read();while(true){if(input.read())break;report();if(flag)break;flag=true;}return false;');
    assert.notDeepStrictEqual(await observe(wrong,row,{value:false}),await observe(expected,row,{value:false}));++mutations;
  }
  if(!row.count&&row.form===39) {
    const wrong=Function('input','report','flag','"use strict";{return !!Math;let Math=false;}');
    assert.notDeepStrictEqual(await observe(wrong,row),await observe(expected,row));++mutations;
  }
  if(!row.count&&row.form===44) {
    const wrong=Function('input','report','flag','"use strict";return input;');
    assert.notDeepStrictEqual(await observe(wrong,row,{otherReceiver:true}),await observe(expected,row,{otherReceiver:true}));++mutations;
  }
  if(!row.count&&(row.form===42||row.form===43)) {
    const duplicate=Function('"use strict";return '+row.source.replace('this.read()','(this.read(),this.read())'))();
    assert.notDeepStrictEqual(await observe(duplicate,row),await observe(expected,row));++mutations;
    const substituted=Function('"use strict";return '+row.source.replace('this.read()','input.read()'))();
    assert.notDeepStrictEqual(await observe(substituted,row,{otherReceiver:true}),await observe(expected,row,{otherReceiver:true}));
    // The input-shadow form has its own TDZ, giving the same observed error.
    if(row.form===42)assert.notDeepStrictEqual(await observe(substituted,row,{beforeSuper:true}),await observe(expected,row,{beforeSuper:true}));++mutations;
  }
  if(!row.count&&row.form===41) {
    const wrong=Function('input','report','flag','"use strict";{return (!!__snapshotFree,!!__snapshotFree);let flag=false;}');
    assert.notDeepStrictEqual(await observe(wrong,row),await observe(expected,row));++mutations;
  }
  if(!row.count&&row.form===37) {
    const wrong=Function('input','report','flag','"use strict";{return (flag=input.read());let flag=false;}');
    assert.notDeepStrictEqual(await observe(wrong,row),await observe(expected,row));++mutations;
  }
  if(!row.count&&[0,2,5,10,19,21,23,25,27,29,32,33,36].includes(row.form)) {
    let wrong;
    if(row.form===0||row.form===19||row.form===21||row.form===23||row.form===25||row.form===27||row.form===29||row.form===32||row.form===33||row.form===36)wrong=row.source.replace('input.read()','(input.read(), input.read())');
    if(row.form===2)wrong=row.source.replace('input.read()','false');
    if(row.form===5)wrong=row.source.replace(/\(flag = input\.read\(\)\)/,'false').replace(/\(0, report\)\(flag, saved\)/,'(0, report)(flag, (flag = input.read()))');
    if(row.form===10)wrong=row.source.replace(/\(0, __snapshotMissing\)/,'__snapshotMissing');
    assert.notEqual(wrong,row.source);parseJavaScript(wrong);
    assert.notDeepStrictEqual(await observe(Function('"use strict";return '+wrong)(),row),await observe(expected,row));++mutations;
  }
}
console.log(`statement-snapshot: ${rows.length} generated functions, ${cases} behavior cases, ${mutations} detected mutations`);
