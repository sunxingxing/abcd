import {recordNodeEvidence} from '../support/isa/oracle_evidence.mjs';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync,mkdirSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-native-member-'));
const run=(exe,args)=>{const p=spawnSync(exe,args,{encoding:'utf8'});assert.equal(p.status,0,p.stderr);return p.stdout;};
let cases=0,mutations=0;
const modes=['normal','get','set','key','key-coercion','key-throw','key-change','key-second-throw','rhs','callee','invoke','noncallable','replace','coercion','coercion-throw','valueOf','valueOf-throw','frozen','reassign'];
const values=[undefined,null,false,0,-0,NaN,Infinity,'3',1n,-9n,Symbol('value')];
function evaluate(exports,name,mode,value) {
  const trace=[],token={mode},state={};let replace;
  const numeric=mode==='valueOf'||mode==='valueOf-throw'?{valueOf(){trace.push('valueOf');if(mode==='valueOf-throw')throw token;return value;}}:mode.startsWith('coercion')?{[Symbol.toPrimitive](hint){trace.push(['numeric',hint]);if(mode==='coercion-throw')throw token;return value;}}:value;
  const property=(object,key)=>Object.defineProperty(object,key,{configurable:true,get(){trace.push(['get',key,this===object]);if(mode==='get')throw token;
    if(mode==='replace')Object.defineProperty(sink,'m',{value(){throw new Error('callee re-read');},configurable:true});
    if(mode==='reassign'&&replace)replace({p:1000});return numeric;},...(mode==='frozen'?{}:{set(v){trace.push(['set',key,this===object,v]);if(mode==='set')throw token;}})});
  property(state,'p');property(state,'q');property(state,'count');const nested={};property(nested,'b');
  Object.defineProperty(state,'a',{get(){trace.push('base');if(mode==='get')throw token;return nested;}});
  const invoke=function(v){'use strict';trace.push(['invoke',this===undefined,v]);if(mode==='invoke')throw token;return v;};
  const sink={};Object.defineProperty(sink,'m',{configurable:true,get(){trace.push('callee');if(mode==='callee')throw token;if(mode==='noncallable')return 1;
    return function(v){trace.push(['method',this===sink,v]);if(mode==='invoke')throw token;return v;};}});
  const key=()=>{trace.push('key');if(mode==='key')throw token;let conversions=0;return mode.startsWith('key-')?{[Symbol.toPrimitive](hint){trace.push(['property-key',hint]);++conversions;if(mode==='key-throw'||mode==='key-second-throw'&&conversions===2)throw token;return mode==='key-change'&&conversions>1?'q':'p';}}:'p';};
  const rhs=()=>{trace.push('rhs');if(mode==='rhs')throw token;return typeof value==='bigint'?2n:2;};
  let result,error;
  try {
    if(name==='makeMutable') {const pair=exports[name](state);replace=pair[1];result=pair[0](sink);}
    else if(name==='makeCaptured')result=exports[name](state)();
    else if(name.startsWith('make'))result=exports[name](state)(sink,key,rhs);
    else if(name==='statement'||name==='multiply')result=exports[name](state);
    else result=exports[name](invoke,state,key,rhs);
  } catch(e){error=e===token?'token':e.constructor.name;}
  return {trace,result:result===numeric && result!==null && typeof result==='object'?{inputObject:true}:result,error};
}
try {
  const manifest=JSON.parse(readFileSync(join(fixture,'manifest.json')));
  for(const [name,hash]of Object.entries(manifest.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,name))).digest('hex'),hash);
  const source=join(temp,'original.mjs');writeFileSync(source,readFileSync(join(fixture,'updates.js')));const original=await import(pathToFileURL(source));
  for(const input of manifest.fixtures) {
    const out=join(temp,input.mode);run(engine,[join(fixture,input.abc),out,'--value-source','--language-source']);
    const record=JSON.parse(readFileSync(join(out,'manifest.json'))).records.find(r=>r.source_status===0);assert.ok(record,'fixture must publish');
    const file=join(out,record.source);run(process.execPath,['--check',file]);const emitted=await import(pathToFileURL(file));
    const evidenceStart=cases;
    for(const name of Object.keys(original))for(const mode of modes)for(const value of values){assert.deepEqual(evaluate(emitted,name,mode,value),evaluate(original,name,mode,value),`${name}/${mode}/${String(value)}`);++cases;}
    assert.match(emitted.makePrefix.toString(),/\+\+\$(?:arg|local)\d+\.p/);
    assert.match(emitted.makePostfix.toString(),/\$(?:arg|local)\d+\.p\+\+/);
    assert.match(emitted.makeCompound.toString(),/\]\s*\+=/);
    for(const [name,pattern,replacement] of [
      ['makePrefix',/\+\+(\$(?:arg|local)\d+\.p)/,'$1++'],
      ['makePrefix',/\+\+(\$(?:arg|local)\d+)\.p/,'++$1.q'],
      ['makePrefix',/\+\+(\$(?:arg|local)\d+\.p)/,'(++$1, ++$1)'],
      ['makePostfix',/(\$(?:arg|local)\d+\.p)\+\+/,'++$1'],
    ]) {
      const text=emitted[name].toString();assert.match(text,pattern);const changed=text.replace(pattern,replacement);assert.notEqual(changed,text);
      const mutant={...emitted,[name]:Function(`return (${changed})`)()};let observed=false;
      for(const mode of modes)for(const value of values){try{assert.deepEqual(evaluate(mutant,name,mode,value),evaluate(original,name,mode,value));}catch(e){assert.ok(e instanceof assert.AssertionError);observed=true;}}
      assert.ok(observed);++mutations;
    }
    for(const name of Object.keys(original))assert.equal(original[name].length,emitted[name].length);
    recordNodeEvidence({abc:join(fixture,input.abc),output:out,original:source,producer:import.meta.url,cases:cases-evidenceStart,engine});
    if(process.env.ABCD_REVIEW_OUT){const root=resolve(process.env.ABCD_REVIEW_OUT,input.mode);mkdirSync(root,{recursive:true});writeFileSync(join(root,'original.mjs'),readFileSync(source));writeFileSync(join(root,'emitted.mjs'),readFileSync(file));}
  }
  const catchesSource=join(temp,'catches-original.mjs');writeFileSync(catchesSource,readFileSync(join(fixture,'catches.js')));
  const catches=await import(pathToFileURL(catchesSource));
  for(const input of manifest.catch_fixtures) {
    const out=join(temp,'catch-'+input.mode);run(engine,[join(fixture,input.abc),out,'--value-source','--language-source']);
    const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records;
    if(!input.publishes) {
      const refused=records.find(r=>r.source_failure);assert.ok(refused);
      assert.ok(refused.source_failure.causes.some(c=>c.domain==='abcd::analysis::CatchRegionError'&&c.value===1));
      continue;
    }
    const record=records.find(r=>r.source_status===0);assert.ok(record);const file=join(out,record.source);
    run(process.execPath,['--check',file]);const emitted=await import(pathToFileURL(file));
    const evidenceStart=cases;
    for(const name of Object.keys(catches))for(const mode of modes)for(const value of values){assert.deepEqual(evaluate(emitted,name,mode,value),evaluate(catches,name,mode,value),`${name}/${mode}/${String(value)}`);++cases;}
    recordNodeEvidence({abc:join(fixture,input.abc),output:out,original:catchesSource,producer:import.meta.url,cases:cases-evidenceStart,engine});
    if(process.env.ABCD_REVIEW_OUT){const root=resolve(process.env.ABCD_REVIEW_OUT,'catch-'+input.mode);mkdirSync(root,{recursive:true});writeFileSync(join(root,'original.mjs'),readFileSync(catchesSource));writeFileSync(join(root,'emitted.mjs'),readFileSync(file));}
  }
  console.log(`native_member_updates: ${cases} differential cases; ${mutations} observed mutants; O0/default native + O0 catch publish; optimized catch retains kHandlerShape`);
} finally{rmSync(temp,{recursive:true,force:true});}
