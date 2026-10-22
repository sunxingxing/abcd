import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(await readFile(p,'utf8')).toString('base64'));
const [old,now]=await Promise.all(process.argv.slice(2,4).map(load));
let cases=0;
// Do not inspect indexed descriptors or compare identities across executions:
// these are precisely the two approved V7 differences. Values, freezing,
// raw/cooked spelling, receiver and effect/exception order are compared.
const snapshot=(s,v)=>({cooked:[...s],raw:[...s.raw],values:v,frozen:Object.isFrozen(s),rawFrozen:Object.isFrozen(s.raw)});
for(const value of [undefined,null,0,-0,NaN,1n,'x','`${\\\n', {p:1}]){
 for(const name of ['plain','escapes','invalid','line','raw','nested','f']){
  if(!(name in old))continue;
  const run=m=>{const trace=[];const tag=function(s,...v){trace.push(['tag',snapshot(s,v),this===undefined]);return trace.length;};
   let result;if(name==='plain')result=m[name](tag,value,7);else if(name==='raw')result=m[name](value);else if(name==='nested')result=m[name](tag,tag,value);else result=m[name](tag,value);
   return {trace,result};};assert.deepEqual(run(now),run(old),name);++cases;
 }
}
for(const name of ['member','effects'])if(name in old){
 for(const failure of ['none','lookup','first','second','tag']){
  const run=m=>{const trace=[];const object={id:1};const saved=function(s,...v){trace.push(['tag',this===object,snapshot(s,v)]);if(failure==='tag')throw Error('tag');return 'ok';};
   Object.defineProperty(object,'tag',{configurable:true,get(){trace.push('lookup');if(failure==='lookup')throw Error('lookup');return saved;}});
   const step=n=>{trace.push(['step',n]);Object.defineProperty(object,'tag',{value(){throw Error('callee re-read');},configurable:true});if(failure===(n===1?'first':'second'))throw Error('step'+n);return n;};
   let result;try{result=name==='effects'?m[name](object,step):m[name](object,3);}catch(e){result=[e.name,e.message];}return {trace,result};};assert.deepEqual(run(now),run(old),name+failure);++cases;
 }
}
if('thrown' in old)for(const fail of ['none','substitution','tag']){
 const run=m=>{const trace=[];const step=()=>{trace.push('step');if(fail==='substitution')throw Error('step');return 4;};const tag=(s,...v)=>{trace.push(snapshot(s,v));if(fail==='tag')throw Error('tag');return 8;};let result;try{result=m.thrown(tag,step);}catch(e){result=[e.name,e.message];}return {trace,result};};assert.deepEqual(run(now),run(old));++cases;
}
if('wide' in old){const run=m=>{const trace=[];const object=new Proxy({},{get(t,k){trace.push(k);return 1;}});const result=m.wide(object,(s,...v)=>snapshot(s,v),8);return {trace,result};};assert.deepEqual(run(now),run(old));++cases;}
console.log(JSON.stringify({cases}));
