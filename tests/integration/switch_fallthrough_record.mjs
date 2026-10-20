// Evaluation-only paired source; the engine receives the ABC, never this text.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const require=createRequire(import.meta.url),acorn=require('internal/deps/acorn/acorn/dist/acorn');
const [structure,root]=process.argv.slice(2),app='DocsSample/NetWork_Kit/NetWorkKit_Datatransmission/Socket';
const input=join(root,'samples/applications_app_samples/harmonysample/projects',app,'artifacts/entry/default/default/modules/modules.abc');
let original=readFileSync(join(root,'samples/applications_app_samples/code',app,'entry/src/main/ets/workers/Tcp2TwoWayTlsWorker.ets'),'utf8');
original=original.replace(/function handleMessage\(message:\s*TlsTwoWayMessage\)/,'function handleMessage(message)');
const start=original.indexOf('function handleMessage(');assert.ok(start>=0);
const parsed=acorn.parseExpressionAt(original,start,{ecmaVersion:'latest'});original=original.slice(parsed.start,parsed.end);
const keys=['type','fileUri','serverIp','serverPort','ca','cert','privateKey','message'];
const names=['loadFile','connectToServer','sendMessageToServer','closeServer'];
function observe(code) {
  const result=[];
  for(const type of ['loadFile','connectServer','sendMessage','closeServer','other'])for(const present of [false,true])
  for(const changing of [false,true])for(const fail of ['',...keys,...names]) {
    const trace=[],message={};let reads=0;
    for(const key of keys)Object.defineProperty(message,key,{get(){trace.push('get:'+key);if(fail===key)throw new TypeError(key);
      if(key==='type')return changing&&reads++?'other':type;return present?key:'';}});
    const functions=names.map(name=>function(...args){assert.equal(this,undefined);trace.push([name,args]);if(fail===name)throw new RangeError(name);});
    const make=new Function(...names,'"use strict"; return ('+code+');');let outcome;
    try{outcome={returned:make(...functions)(message)};}catch(e){outcome={thrown:e.constructor.name};}
    result.push({type,present,changing,fail,trace,outcome});
  }
  return result;
}
const expected=observe(original),dir=mkdtempSync(join(tmpdir(),'abcd-switch-fallthrough-record-'));
try{
  const out=join(dir,'source'),p=spawnSync(structure,[input,out,'--value-source','--language-source'],{encoding:'utf8'});assert.equal(p.status,0,p.stderr);
  const row=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.entity===57687);assert.equal(row.source_status,0,JSON.stringify(row));
  const file=join(out,row.source),text=readFileSync(file,'utf8');assert.equal(spawnSync(process.execPath,['--check',file],{encoding:'utf8'}).status,0);
  const at=/handleMessage =\s*/.exec(text);assert.ok(at);const node=acorn.parseExpressionAt(text,at.index+at[0].length,{ecmaVersion:'latest'});
  const actual=text.slice(node.start,node.end);assert.ok(actual.includes('switch ('));assert.deepEqual(observe(actual),expected);
  const mutant=actual.replace('sendMessageToServer(message.message);','sendMessageToServer(message.message); break;');assert.notEqual(mutant,actual);assert.notDeepEqual(observe(mutant),expected);
  console.log('o18/57687 handleMessage: '+expected.length+' independent paired-source scenarios; missing fall-through mutation observed');
}finally{rmSync(dir,{recursive:true,force:true});}
