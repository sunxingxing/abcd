import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(await readFile(p)).toString('base64'));
const [old,now]=await Promise.all(process.argv.slice(2,4).map(load));
let cases=0;
for (const fail of [false,true]) {
 const run=m=>{const trace=[];const f=function(...v){trace.push(['call',this===undefined,v]);if(fail)throw Error('call');return v.length;};const o={};Object.defineProperty(o,'f',{get(){trace.push('get');return function(...v){trace.push(['member',this===o,v]);if(fail)throw Error('member');return v.length;};}});const C=function(...v){trace.push(['new',v]);if(fail)throw Error('new');this.n=v.length;};const tag=(s,...v)=>{trace.push(['tag',[...s],[...s.raw],v]);if(fail)throw Error('tag');return v.length;};const result=[];for(const [name,args] of [['direct',[f]],['member',[o]],['construct',[C]],['template',[tag,9]]]){if(!(name in m))continue;try{let r=m[name](...args);result.push(name==='construct'?r.n:r);}catch(e){result.push([e.name,e.message]);}}return {trace,result};};
 assert.deepEqual(run(now),run(old));++cases;
}
if (old.effects) for (const failure of [-1,0,100,799]) {
 const run=m=>{const trace=[];const o={};const saved=function(...v){trace.push(['invoke',this===o,v.length]);return v.reduce((a,b)=>a+b,0);};Object.defineProperty(o,'f',{configurable:true,get(){trace.push('lookup');return saved;}});const step=i=>{trace.push(i);Object.defineProperty(o,'f',{value(){throw Error('re-read');},configurable:true});if(i===failure)throw Error('arg'+i);return i;};let value;try{value=m.effects(o,step);}catch(e){value=[e.name,e.message];}return {trace,value};};assert.deepEqual(run(now),run(old));++cases;
}
if(old.Derived){const a=new old.Derived(),b=new now.Derived();assert.equal(b instanceof now.Derived,true);assert.deepEqual([...b],[...a]);++cases;}
console.log(JSON.stringify({cases}));
