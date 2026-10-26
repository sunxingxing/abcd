// Application source is an independent oracle, never an engine input.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const require=createRequire(import.meta.url),acorn=require('internal/deps/acorn/acorn/dist/acorn');
assert.equal(acorn.version,'8.15.0');
const [structure,root]=process.argv.slice(2);
const relative='DocsSample/DriverDevelopmentKit/ScsiPeripheralDemo/entry/src/main/ets/component/SendCommandComponent.ets';
const input=join(root,'samples/applications_app_samples/harmonysample/projects/DocsSample/DriverDevelopmentKit/ScsiPeripheralDemo/artifacts/entry/default/default/modules/modules.abc');
const oracle=readFileSync(join(root,'samples/applications_app_samples/code',relative),'utf8');
function callback(text) {
  const marker='.onClick(', start=text.indexOf(marker)+marker.length;
  assert.ok(start>=marker.length);
  const expression=acorn.parseExpressionAt(text,start,{ecmaVersion:'latest'});
  assert.equal(expression.type,'ArrowFunctionExpression');
  return text.slice(expression.start,expression.end);
}
const original=callback(oracle);
const kinds={READ_CAPACITY:1,TEST_UNIT_READY:2,READ:3,WRITE:4,VERIFY:5,INQUIRY:6,REQUEST_SENSE:7};
const rpcNames=['getInstance','testUnitReady','readBlockData','verify','writeBlockData','inquiry','requestSense','readCapacity'];
const fields=['index','blockStartAddr','blockNum','dataOfSendCommandPage'];
const failNames=[...fields,...rpcNames,'popMessage','info'];
function observe(code) {
  const results=[];
  for(let kind=0;kind<=8;kind++) for(const address of [-1,0]) for(const count of [0,1])
  for(const data of [[],[4]]) for(const changing of [false,true])
  for(const fail of ['',...failNames.flatMap(n=>['get:'+n,'call:'+n])]) {
    const trace=[],state={},tool={},rpc={},hilog={};let indexReads=0;
    const access=(name,value)=>{trace.push('get:'+name);if(fail==='get:'+name)throw new TypeError(name);return value;};
    const method=(owner,name,value)=>Object.defineProperty(owner,name,{get(){
      return access(name,function(...args){
        assert.equal(this,owner);trace.push(['call:'+name,...args]);
        if(fail==='call:'+name)throw new RangeError(name);return value;
      });
    }});
    for(const field of fields) Object.defineProperty(state,field,{get(){
      const value=field==='index' ? (indexReads++ && changing ? kinds.VERIFY : kind) :
        field==='blockStartAddr'?address:field==='blockNum'?count:data;
      return access(field,value);
    }});
    method(state,'popMessage');method(tool,'getInstance',rpc);method(hilog,'info');
    for(const name of rpcNames.slice(1))method(rpc,name);
    const build=new Function('$local0','CommandType','RpcTool','hilog','return '+code);
    let result;
    try{result={value:build.call(state,state,kinds,tool,hilog)()};}
    catch(e){result={exception:e.constructor.name};}
    results.push({kind,address,count,data,changing,fail,result,trace});
  }
  return results;
}
const expected=observe(original),dir=mkdtempSync(join(tmpdir(),'abcd-shared-terminal-record-'));
try {
  assert.equal(expected.length,4176);
  for(const [name,flags]of[['default',[]],['source',['--language-source']]]){
    const out=join(dir,name),p=spawnSync(structure,[input,out,'--value-source',...flags],{encoding:'utf8'});
    assert.equal(p.status,0,p.stderr);
    const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.entity===13920);
    assert.equal(record.source_complete,true,JSON.stringify(record));
    const file=join(out,record.source),text=readFileSync(file,'utf8');
    assert.equal(spawnSync(process.execPath,['--check',file],{encoding:'utf8'}).status,0);
    const actual=callback(text);
    assert.deepEqual(observe(actual),expected,name);
    const mutation=actual.replace(/return void 0;/g,'void 0;');
    assert.notEqual(mutation,actual);assert.notDeepEqual(observe(mutation),expected);
  }
  console.log('o38/13920: '+expected.length+' original/emitted callback scenarios per flag set');
}finally{rmSync(dir,{recursive:true,force:true});}
