import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(await readFile(p,'utf8')).toString('base64'));
const [old,now]=await Promise.all(process.argv.slice(2).map(load));
if('dynamic' in old){
 function run(module){const trace=[];const source={get a(){trace.push('get:a');return 1;},get b(){trace.push('get:b');return 2;},'undefined':9};const result=module.dynamic(source,()=>{trace.push('hook');return {[Symbol.toPrimitive](hint){trace.push('key:'+hint);return 'a';}};});return {result,trace};}
 assert.deepEqual(run(now),run(old));
 for(const key of ['a','b','undefined','',0,undefined])assert.deepEqual(now.dynamic({a:1,b:2,0:3,undefined:4},()=>key),old.dynamic({a:1,b:2,0:3,undefined:4},()=>key));
 for(const module of [old,now]){let called=false;assert.throws(()=>module.dynamic(null,()=>{called=true;return 'a';}),TypeError);assert.equal(called,false);const marker={error:true};assert.throws(()=>module.dynamic({},()=>{throw marker;}),e=>e===marker);assert.equal(module.dynamic.length,2);}
 console.log(JSON.stringify({cases:13}));process.exit(0);
}
if('wide' in old){
 const text=await readFile(process.argv[2],'utf8');const count=(text.match(/k[0-9]+/g)||[]).length/2;
 function run(module){const trace=[];const input={tail:17};for(let i=0;i<count;++i)Object.defineProperty(input,'k'+i,{enumerable:true,configurable:true,get(){trace.push(i);assert.equal(this,input);return i;}});const result=module.wide(input);assert.deepEqual(trace,Array.from({length:count},(_,i)=>i));return {result,trace};}
 assert.deepEqual(run(now),run(old));for(const input of [null,undefined])for(const module of [old,now])assert.throws(()=>module.wide(input),TypeError);
 console.log(JSON.stringify({cases:5}));process.exit(0);
}
if('f' in old){for(const source of [null,undefined,3,false,'abc',{}, {a:1},Object.create({inherited:2})]){let a,b;try{a=old.f(source);}catch(e){a=e.constructor.name;}try{b=now.f(source);}catch(e){b=e.constructor.name;}assert.deepEqual(b,a);}console.log(JSON.stringify({cases:8}));process.exit(0);}
function exercise(m){
 const results=[],trace=[];const symbol=Symbol.for('rest');
 for(const value of [null,undefined,3,false,'abc',{}, {a:1,b:2,c:3},Object.create({a:2}),{[symbol]:4,a:1}]) {
  for(const name of ['pick','assign','only','computed','copied']) {
   try{const out=m[name](value);results.push([name,out]);}catch(error){results.push([name,error.constructor.name]);}
  }
 }
 for(const throwing of ['', 'a','b','c']) {
  const source={get a(){trace.push('a');if(throwing==='a')throw new RangeError('a');return 1;},get b(){trace.push('b');if(throwing==='b')throw new RangeError('b');return 2;},get c(){trace.push('c');if(throwing==='c')throw new RangeError('c');return 3;}};
  try{results.push(m.pick(source));}catch(error){results.push([error.constructor.name,error.message]);}
 }
 return {results,trace};
}
assert.deepEqual(exercise(now),exercise(old));
for(const marker of [null,undefined,0,Symbol.for('thrown'),{marker:1}])for(const module of [old,now]){let caught=false;try{module.pick({get a(){throw marker;}});}catch(error){caught=true;assert.equal(error,marker);}assert(caught);}
const descriptor=Object.getOwnPropertyDescriptor(now.only({x:1}),'x');assert.deepEqual(descriptor,{value:1,writable:true,enumerable:true,configurable:true});
// These are native-source checks. Ark's Set-vs-define and Proxy unwrap are the
// two V7 differences, explicitly excluded from the Ark equivalence claim.
let setters=0;Object.defineProperty(Object.prototype,'restTrap',{configurable:true,set(){++setters;}});
try{assert.equal(now.only({restTrap:3}).restTrap,3);assert.equal(setters,0);}finally{delete Object.prototype.restTrap;}
console.log(JSON.stringify({cases:57,exclusions:['V7: [[Set]] vs CreateDataProperty','V7: Proxy source unwrap bypasses traps on Ark']}));
