import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(await readFile(p)).toString('base64'));
const sources=await Promise.all(process.argv.slice(2,4).map(p=>readFile(p,'utf8')));
let cases=0;
for(const fail of [false,true])for(const value of [undefined,null,0,-0,NaN,1n,'x',{}]) {
 const run=async text=>{const {A}=await import('data:text/javascript;base64,'+Buffer.from(text+'\n//'+cases).toString('base64'));const trace=[];const receiver=new A();
  const proto={};Object.defineProperty(proto,'value',{set(v){trace.push(['set',this===receiver?'instance':this===A?'static':'wrong',v]);if(fail)throw Error('setter');},configurable:true});Object.setPrototypeOf(A.prototype,proto);Object.setPrototypeOf(A,proto);
  const results=[];for(const who of [receiver,A])for(const name of ['named','computed'])if(typeof who[name]==='function'){try{results.push(who[name](value));}catch(e){results.push([e.name,e.message]);}}
  return {trace,results};};assert.deepEqual(await run(sources[0]),await run(sources[1]));++cases;
}
console.log(JSON.stringify({cases}));
