// The paired .ets callback is evaluation only; only the ABC enters the engine.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const require=createRequire(import.meta.url),acorn=require('internal/deps/acorn/acorn/dist/acorn');
assert.equal(acorn.version,'8.15.0');
const [structure,root]=process.argv.slice(2);
const app='DocsSample/Media/Audio/AudioRoutingAndVolumeSample';
const input=join(root,'samples/applications_app_samples/harmonysample/projects',app,'artifacts/entry/default/default/modules/modules.abc');
let oracle=readFileSync(join(root,'samples/applications_app_samples/code',app,'entry/src/main/ets/pages/AudioOutputDeviceChange.ets'),'utf8');
// Strip only this oracle parameter's TS annotation, not any executable body.
oracle=oracle.replace('(deviceChangeInfo: audio.AudioStreamDeviceChangeInfo)', '(deviceChangeInfo)');
function callback(text){
  const found=/\.on\((['"])outputDeviceChangeWithInfo\1,\s*/.exec(text);assert.ok(found);
  const node=acorn.parseExpressionAt(text,found.index+found[0].length,{ecmaVersion:'latest'});
  assert.equal(node.type,'ArrowFunctionExpression');return text.slice(node.start,node.end);
}
const original=callback(oracle);
const keys=['REASON_OLD_DEVICE_UNAVAILABLE','REASON_NEW_DEVICE_AVAILABLE','REASON_OVERRODE','REASON_UNKNOWN'];
function observe(code){
  const result=[];
  for(const reason of [0,1,2,3,4])for(const count of [0,2])for(const enabled of [false,true])
  for(const renderer of ['object','null','undefined'])for(const changing of [false,true])
  for(const fail of ['', 'reason','devices','pause','call:pause','enum','info','update']){
    const trace=[],enums={},audio={AudioStreamDeviceChangeReason:enums},log={},device={};let reads=0;
    const get=(key,value)=>{trace.push('get:'+key);if(fail===key)throw new TypeError(key);return value;};
    for(const [i,k]of keys.entries())Object.defineProperty(enums,k,{get(){return get('enum',i);}});
    Object.defineProperty(device,'changeReason',{enumerable:true,get(){return get('reason',changing&&reads++?0:reason);}});
    const devices=Array.from({length:count},(_,i)=>({id:i,name:'device'+i,deviceType:i+10}));
    Object.defineProperty(device,'devices',{enumerable:true,get(){return get('devices',devices);}});
    const owner={};Object.defineProperty(owner,'pause',{get(){return get('pause',function(){assert.equal(this,owner);trace.push('pause');if(fail==='call:pause')throw new RangeError('pause');});}});
    log.info=function(x){assert.equal(this,log);trace.push(['info',x]);if(fail==='info')throw new SyntaxError('info');};
    const update=enabled?function(x){trace.push(['update',x]);if(fail==='update')throw new ReferenceError('update');}:undefined;
    const value=renderer==='null'?null:renderer==='undefined'?undefined:owner;
    const make=new Function('audio','audioRenderer','globalCallbackUpdate','console','return '+code);
    let outcome;try{outcome={returned:make(audio,value,update,log)(device)};}catch(e){outcome={thrown:e.constructor.name};}
    result.push({reason,count,enabled,renderer,changing,fail,trace,outcome});
  }
  return result;
}
const expected=observe(original),dir=mkdtempSync(join(tmpdir(),'abcd-switch-record-'));
try{
  assert.equal(expected.length,960);
  const out=join(dir,'source'),exported=spawnSync(structure,[input,out,'--value-source','--language-source'],{encoding:'utf8'});
  assert.equal(exported.status,0,exported.stderr);
  const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.entity===18254);
  assert.equal(row.source_complete,true,JSON.stringify(row));
  const file=join(out,row.source),text=readFileSync(file,'utf8');
  assert.equal(spawnSync(process.execPath,['--check',file],{encoding:'utf8'}).status,0);
  const actual=callback(text);assert.deepEqual(observe(actual),expected);
  const mutant=actual.replace('?.pause()','.pause()');assert.notEqual(mutant,actual);assert.notDeepEqual(observe(mutant),expected);
  console.log('o19/18254: '+expected.length+' independent original/emitted callback scenarios');
}finally{rmSync(dir,{recursive:true,force:true});}
