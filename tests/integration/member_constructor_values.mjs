import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync,mkdirSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-member-constructors-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
let cases=0,mutations=0;
function evaluate(fn,name,flag,mode,value) {
  const trace=[],token={thrown:mode},receiver={};
  Object.defineProperty(receiver,'create',{configurable:true,get(){trace.push('callee');if(mode==='callee')throw token;
    if(mode==='noncallable')return 1;return function(value){trace.push(['invoke',this===receiver,value]);return value;};}});
  const source={};for(const key of ['a','b'])Object.defineProperty(source,key,{get(){trace.push(key);if(mode===key)throw token;return value;}});
  for(const key of ['g','vp'])Object.defineProperty(source,key,{get(){trace.push(key+'-get');if(mode===key+'-get')throw token;
    return function(...args){trace.push([key,this===source,args]);if(mode===key)throw token;
      if(mode==='replace')Object.defineProperty(receiver,'create',{value(){throw new Error('callee reread');},configurable:true});return value;};}});
  const make=key=>function(argument){trace.push(['new',key,new.target===this.constructor,argument]);if(mode===key)throw token;this.key=key;this.argument=argument;};
  const A=make('A'),B=make('B');
  let result,error;try{result=fn(receiver,flag,A,B,source);}catch(e){error=e===token?'token':e.constructor.name;}
  const normalize=x=>JSON.parse(JSON.stringify(x,(_k,v)=>typeof v==='bigint'?['bigint',String(v)]:typeof v==='symbol'?['symbol']:v));
  return normalize({trace,result,error});
}
function compareConstructors(original,emitted,name) {
  for(const flag of [false,true,0,1,null,undefined,'yes',{}])for(const mode of ['normal','callee','noncallable','a','b','g-get','g','vp-get','vp','A','B','replace'])
    for(const value of [null,0,false,'x',19n,{identity:1}]) {
      assert.deepEqual(evaluate(emitted,name,flag,mode,value),evaluate(original,name,flag,mode,value),`${name}/${mode}/${String(flag)}`);++cases;
    }
}
function evaluateUpdate(fn,name,mode,value) {
  const trace=[],token={mode},o={};
  const input=mode==='valueOf'||mode==='valueOf-throw'?{valueOf(){trace.push('valueOf');if(mode==='valueOf-throw')throw token;return value;}}:mode==='coercion'||mode==='coercion-throw'?{[Symbol.toPrimitive](hint){trace.push(['numeric',hint]);if(mode==='coercion-throw')throw token;return value;}}:value;
  Object.defineProperty(o,'p',{get(){trace.push('get');if(mode==='get')throw token;
    if(mode==='replace')Object.defineProperty(invoke,'m',{value(){throw new Error('callee reread');},configurable:true});return input;},set(v){trace.push(['set',v]);if(mode==='set')throw token;}});
  const invoke=function(v){'use strict';trace.push(['invoke',this===undefined,v]);if(mode==='invoke')throw token;return v;};
  Object.defineProperty(invoke,'m',{configurable:true,get(){trace.push('callee');if(mode==='callee')throw token;return function(v){trace.push(['method',this===invoke,v]);if(mode==='invoke')throw token;return v;};}});
  const k=()=>{trace.push('key');if(mode==='key')throw token;return mode==='key-object'||mode==='key-throw-conversion'?{[Symbol.toPrimitive](hint){trace.push(['property-key',hint]);if(mode==='key-throw-conversion')throw token;return 'p';}}:'p';};
  const g=()=>{trace.push('rhs');if(mode==='rhs')throw token;return typeof value==='bigint'?2n:2;};
  let result,error;try{result=fn(invoke,o,k,g);}catch(e){error=e===token?'token':e.constructor.name;}
  return {trace,result,error};
}
try {
  const manifest=JSON.parse(readFileSync(join(fixture,'manifest.json')));
  for(const [file,hash] of Object.entries(manifest.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,file))).digest('hex'),hash);
  const originalPath=join(temp,'original.mjs');writeFileSync(originalPath,readFileSync(join(fixture,'calls.js')));
  const original=await import(pathToFileURL(originalPath));
  for(const input of manifest.fixtures) {
    const out=join(temp,input.mode);run(engine,[join(fixture,input.abc),out,'--value-source','--language-source']);
    const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status===0);assert.ok(record);
    const file=join(out,record.source);run(process.execPath,['--check',file]);const emitted=await import(pathToFileURL(file));
    for(const name of ['branches','nested','simple'])compareConstructors(original[name],emitted[name],name);
    // These ordinary parameter-target updates already published before this
    // round. Their existing lowering is checked, not claimed as native member
    // Reference admission or as evidence for a captured mutable base.
    for(const name of ['prefix','postfix','compound','savedMember'])for(const mode of ['normal','get','set','key','rhs','invoke','callee','coercion','coercion-throw','replace','valueOf','valueOf-throw','key-object','key-throw-conversion'])
      for(const value of [undefined,null,false,0,-0,NaN,Infinity,1n,-9n,'3',Symbol('value')]) {
        assert.deepEqual(evaluateUpdate(emitted[name],name,mode,value),evaluateUpdate(original[name],name,mode,value));++cases;
      }
    const text=emitted.nested.toString();
    for(const [before,after] of [['$arg0.create(', '(0, $arg0.create)('],['new $arg2(', 'new $arg3('],['$arg1 ?', '!$arg1 ?'],['$arg4.vp(10)', '$arg4.vp(20)'],['new $arg2({space: $arg4.vp(10)})','(new $arg2({space: $arg4.vp(10)}), new $arg2({space: $arg4.vp(10)}))']]) {
      assert.ok(text.includes(before));const changed=text.replace(before,after);let observed=false;const positiveCases=cases;
      try{const fn=Function(`return (${changed})`)();compareConstructors(original.nested,fn,'nested');}catch(e){assert.ok(e instanceof assert.AssertionError);observed=true;}
      cases=positiveCases;assert.ok(observed);++mutations;
    }
    // Method arities and complete-record publication are part of the fixture.
    for(const name of Object.keys(original))assert.equal(emitted[name].length,original[name].length);
    if(process.env.ABCD_REVIEW_OUT) {
      const root=resolve(process.env.ABCD_REVIEW_OUT,input.mode);mkdirSync(root,{recursive:true});writeFileSync(join(root,'original.mjs'),readFileSync(originalPath));writeFileSync(join(root,'emitted.mjs'),readFileSync(file));
    }
  }
  console.log(`member_constructor_values: ${cases} differential cases; ${mutations} observed mutations; both optimization modes publish`);
} finally {rmSync(temp,{recursive:true,force:true});}
