import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(await readFile(p,'utf8')).toString('base64'));
const [old,now]=await Promise.all(process.argv.slice(2).map(load));
let cases=0;
const descriptors=o=>Reflect.ownKeys(o).map(k=>[k,Object.getOwnPropertyDescriptor(o,k)]);
for(const p of [null,undefined,false,3,1n,Symbol.for('proto'),'abc',{},Object.create(null),()=>4]) {
  if(old.make) {
    const a=old.make(p),b=now.make(p);assert.equal(Object.getPrototypeOf(a),Object.getPrototypeOf(b));assert.deepEqual(descriptors(a),descriptors(b));
    assert.equal(Object.getPrototypeOf(b),p===null||typeof p==='object'||typeof p==='function'?p:Object.prototype);
    const x=old.nested(p),y=now.nested(p);assert.equal(Object.getPrototypeOf(x),null);assert.equal(Object.getPrototypeOf(y),null);assert.equal(Object.getPrototypeOf(x.inner),Object.getPrototypeOf(y.inner));
    cases+=2;
  } else {const a=old.wide({},p),b=now.wide({},p);assert.equal(Object.getPrototypeOf(a),Object.getPrototypeOf(b));assert.deepEqual(descriptors(a),descriptors(b));cases++;}
}
if(old.ordered) {
  function run(m,fail){const trace=[];const p={inherited:7};const f=n=>()=>{trace.push(n);if(n===fail)throw new RangeError(n);return n==='proto'?p:n;};try{const x=m.ordered(f('first'),f('proto'),f('last'));return [trace,Object.getPrototypeOf(x)===p,descriptors(x)];}catch(e){return [trace,e.name,e.message];}}
  for(const fail of [undefined,'first','proto','last']){assert.deepEqual(run(now,fail),run(old,fail));cases++;}
  const a=now.fresh(),b=now.fresh();assert.notEqual(a,b);assert.notEqual(Object.getPrototypeOf(a),Object.getPrototypeOf(b));assert.equal(a.inherited,9);cases++;
}
console.log(JSON.stringify({cases}));
