// Unpublished production draft rendered through the direct AST test printer.
// No ConstructedFunction/VerifiedFunction/source receipt is manufactured.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [builder,abc,source]=process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-for-of-draft-'));
try {
  const output=join(dir,'draft.mjs');
  execFileSync(builder,[abc,'387',`--output=${output}`],{timeout:120000});
  execFileSync(process.execPath,['--check',output]);
  const draft=(await import(pathToFileURL(output))).default;
  const oracle=(await import(`data:text/javascript;base64,${Buffer.from(readFileSync(source)).toString('base64')}`)).scan;
  const ordinary=[()=>[],()=>[1,2,3],()=>['a','b'],()=> 'a𝄞'];
  function trace(fn,make,throwAt) {
    const log=[];let count=0;
    try{log.push(['result',fn(make(log),value=>{log.push(['visit',value]);if(++count===throwAt)throw new RangeError('body');})]);}
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
} finally {rmSync(dir,{recursive:true,force:true});}
