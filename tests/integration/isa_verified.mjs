// Both programs are evaluated independently; the engine receives only ABC.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {pathToFileURL} from 'node:url';
const [original,emitted,origin]=process.argv.slice(2);
let cases=0;
if(origin==='constructed') {
  const observe=(source,kind,text)=>{
    const trace=[];
    const input=kind==='null'?null:new Proxy({}, {get(target,key){trace.push(['get',key]);if(kind==='throw')throw new RangeError('get');return 17;},set(target,key,value){trace.push(['set',key,value]);if(kind==='throw')throw new RangeError('set');return kind!=='false';}});
    const context=vm.createContext({input});let error;
    try{vm.runInContext("'use strict';\n"+(text??readFileSync(source,'utf8')).replace(/^export \{\};?\s*$/gm,''),context);}catch(e){error=[e.name,e.message];}
    return {trace,error};
  };
  for(const kind of ['value','throw','null','false']){assert.deepEqual(observe(emitted,kind),observe(original,kind));++cases;}
  const before=readFileSync(emitted,'utf8');const changed=before.replace(/\[(?:[0-9]+)\]/,'[7]');
  assert.notEqual(before,changed,'index mutation must change printed program');
  assert.notDeepEqual(observe(emitted,'value',changed),observe(original,'value'),'wrong index must be observed');
} else {
  const actual=await import(pathToFileURL(emitted));
  const expected=await import('data:text/javascript;base64,'+Buffer.from(readFileSync(original)).toString('base64'));
  const observe=(fn,args)=>{try{return {value:fn(...args)};}catch(e){return {error:[e.name,e.message]};}};
  if ('wideNew' in actual) {
    for (const throws of [false,true]) {
      const run=(module)=>{const log=[];const C=function(...xs){log.push(xs);if(throws)throw new RangeError('constructor');this.args=xs;};const result=observe(module.wideNew,[C]);return {log,result:result.error??result.value.args};};
      assert.deepEqual(run(actual),run(expected));++cases;
    }
  } else if ('WideArray' in actual) {
    const a=new actual.WideArray(),b=new expected.WideArray();
    assert.deepEqual(Array.from(a),Array.from(b));assert.equal(a.length,270);
    assert.equal(Object.getPrototypeOf(Object.getPrototypeOf(a)),Array.prototype);
    assert.equal(actual.WideArray.length,expected.WideArray.length);++cases;
  } else {
  for(const name of ['xor','power']) for(const a of [0,-0,NaN,2,-1,3n,undefined,null,Symbol('x')]) for(const b of [0,2,-1,2n,undefined]) {
    assert.deepEqual(observe(actual[name],[a,b]),observe(expected[name],[a,b]));++cases;
  }
  const trace=(module,name,fail)=>{
    const log=[];
    const input=new Proxy({}, {get(target,key){log.push(key);if(key===fail)throw new SyntaxError('indexed');return key;}});
    return {result:observe(module[name],[input]),log};
  };
  for(const name of ['indexed','indexed16'])for(const fail of [undefined,'0','200','65535','65536','4294967295','139']){
    assert.deepEqual(trace(actual,name,fail),trace(expected,name,fail));++cases;
  }
  for(const name of ['xor','power']){
    const trace=(module)=>{const log=[];const left={valueOf(){log.push('left');return 2;}};const right={valueOf(){log.push('right');throw new TypeError('right');}};return {result:observe(module[name],[left,right]),log};};
    assert.deepEqual(trace(actual),trace(expected));++cases;
  }
  }
}
console.log(JSON.stringify({cases}));
