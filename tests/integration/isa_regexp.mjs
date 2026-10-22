import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(await readFile(p,'utf8')).toString('base64'));
const [old,now]=await Promise.all(process.argv.slice(2,4).map(load));
let cases=0;
for(const name of Object.keys(old).filter(n=>n!=='wide'&&n!=='loop')){
 const factory=m=>{const r=m[name]();return [r.source,r.flags,r.lastIndex,Object.getPrototypeOf(r)===RegExp.prototype];};assert.deepEqual(factory(now),factory(old));++cases;
 for(const input of ['','aaab','a/b','/','a\nb','abc','bc','AB','a\nb\nc','\n\r\u2028\u2029']){
  const run=m=>{const r=m[name]();const result=[];for(let i=0;i<3;i++){const match=r.exec(input);result.push([match&&[...match],match?.index,match?.indices,r.lastIndex]);}return result;};assert.deepEqual(run(now),run(old));++cases;
 }
 const a=now[name](),b=now[name]();assert.notEqual(a,b);a.lastIndex=9;assert.equal(b.lastIndex,0);++cases;
}
if('loop' in old){assert.deepEqual(now.loop(),old.loop());++cases;}
if('wide' in old){const run=m=>{const trace=[];const value=m.wide(new Proxy({},{get(o,k){trace.push(k);return 1;}}));return [value.source,value.flags,trace];};assert.deepEqual(run(now),run(old));++cases;}
// Repeated evaluations remain fresh and have independent lastIndex.
if('globalFlag' in now)for(let i=0;i<30;i++){const a=now.globalFlag();a.exec('a');assert.equal(a.lastIndex,1);assert.equal(now.globalFlag().lastIndex,0);++cases;}
// An intrinsic literal must not become a mutable global constructor lookup.
if(process.argv[4]==='constructed'&&'basic' in now){
 const Saved=globalThis.RegExp;
 try { globalThis.RegExp=function(){throw Error('global constructor read');};
  assert.equal(now.basic().source,old.basic().source);++cases;
 } finally { globalThis.RegExp=Saved; }
}
console.log(JSON.stringify({cases}));
