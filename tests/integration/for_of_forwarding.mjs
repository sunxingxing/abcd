// Production engine export: joint OriginalFlow/recovery, never direct draft printing.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [structure,probe,abc,source,compiler]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-for-of-forwarding-'));
try {
  const out=join(dir,'engine');
  execFileSync(structure,[abc,out,'--value-source','--language-source'],{timeout:120000});
  const manifest=JSON.parse(readFileSync(join(out,'manifest.json')));
  const record=manifest.records.find(r=>r.source_status===0);assert.ok(record,'engine must publish a complete record');
  const output=join(out,record.source);
  const text=readFileSync(output,'utf8');assert.match(text,/for \([^)]* of /);
  // Pinned es2abc predates ES2022 string export names. Only identifier-valued
  // aliases are unquoted at this compiler boundary; function text is exact.
  const js=join(dir,'roundtrip.js'),roundtrip=join(dir,'roundtrip.abc');
  writeFileSync(js,text.replace(/ as "([A-Za-z_$][\w$]*)"/g,' as $1'));
  execFileSync(compiler,['--module','--merge-abc','--opt-level=0','--output='+roundtrip,js]);
  const meta=join(dir,'metadata');execFileSync(structure,[roundtrip,meta]);
  const roundManifest=JSON.parse(readFileSync(join(meta,'manifest.json')));
  const tables=roundManifest.records.map(r=>JSON.parse(readFileSync(join(meta,r.metadata))));
  const method=tables.flatMap(r=>r.methods).find(m=>m.abi?.parameter_slots===2);assert.ok(method);
  // Re-import immutable original flow/SSA and reprove acquisition, cached next,
  // object check, done/value, receiver, iteration and every effectful close.
  execFileSync(probe,[roundtrip,String(method.entity),'--protocol-only'],{timeout:120000});
  execFileSync(process.execPath,['--check',output]);
  const draft=(await import(pathToFileURL(output))).scan;
  const oracle=(await import(`data:text/javascript;base64,${Buffer.from(readFileSync(source)).toString('base64')}`)).scan;
  const ordinary=[()=>[],()=>[1,2,3],()=>['a','b'],()=> 'a𝄞'];
  function trace(fn,make,throwAt) {
    const log=[];let count=0;
    try{log.push(['result',fn({get(){log.push("rhs-call");return make(log);}},value=>{log.push(['visit',value]);if(++count===throwAt)throw new RangeError('body');})]);}
    catch(error){log.push(['throw',error.constructor.name]);}
    return log;
  }
  for(const make of ordinary)for(const at of [0,1,2])assert.deepEqual(trace(draft,make,at),trace(oracle,make,at));
  for(const close of ['object','null','missing','getter-throw','call-throw','primitive']) {
    const make=log=>({[Symbol.iterator](){
      log.push(['acquire']);let step=0;
      const iterator={get next(){log.push(['next-get']);return function(){log.push(['next',this===iterator]);return step++<2?{done:false,value:step}:{done:true};};}};
      if(close!=='missing')Object.defineProperty(iterator,'return',{get(){
        log.push(['return-get']);if(close==='getter-throw')throw new SyntaxError('close');if(close==='null')return null;
        return function(){log.push(['return-call',this===iterator]);if(close==='call-throw')throw new TypeError('close');return close==='primitive'?1:{};};
      }});
      return iterator;
    }});
    for(const at of [0,1,2])assert.deepEqual(trace(draft,make,at),trace(oracle,make,at),`${close}/${at}`);
  }
  for(const where of ['next-get','next-call','done-get','value-get']) {
    const make=log=>({[Symbol.iterator](){return {
      get next(){log.push('next-get');if(where==='next-get')throw new SyntaxError();return function(){
        log.push('next-call');if(where==='next-call')throw new TypeError();return {
          get done(){log.push('done-get');if(where==='done-get')throw new RangeError();return false;},
          get value(){log.push('value-get');if(where==='value-get')throw new EvalError();return 1;}
        };};},return(){log.push('return');return {};}
    };}});
    assert.deepEqual(trace(draft,make,1),trace(oracle,make,1),where);
  }
  for(const where of ['getter','call']) {
    const run=fn=>{const log=[];const input={get get(){log.push('rhs-get');if(where==='getter')throw new SyntaxError();return ()=>{log.push('rhs-call');throw new RangeError();};}};
      try{fn(input,x=>log.push(x));}catch(e){log.push(e.constructor.name);}return log;};
    assert.deepEqual(run(draft),run(oracle));
  }
  assert.doesNotMatch(text,/\btry\b/,'the verified forwarding wrapper has no source try');
  console.log('production native for-of: Node values/order/live-out/throws/close priority; emitted output recompiled and protocol reverified');
} finally {rmSync(dir,{recursive:true,force:true});}
