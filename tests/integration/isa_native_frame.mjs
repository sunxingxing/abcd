import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original,emitted,kind]=process.argv.slice(2);
const load=async path=>{const text=readFileSync(path,'utf8');const m=await import('data:text/javascript;base64,'+Buffer.from(text).toString('base64'));return m;};
const a=await load(original),b=await load(emitted);let cases=0;
if(kind==='LDNEWTARGET'||kind==='sdk'){
 const name=kind==='sdk'?'target':'f';
 for(const ns of [a,b]){const f=ns[name];assert.equal(f(),undefined);assert.equal(new f(),f);function Other(){}assert.equal(Reflect.construct(f,[],Other),Other);cases+=3;}
 if(kind==='sdk'){assert.equal(new a.A().target,a.A);assert.equal(new b.A().target,b.A);cases+=2;}
 const text=readFileSync(emitted,'utf8'),mutant=text.replaceAll('new.target','undefined');assert.notEqual(text,mutant);
 const ns=await import('data:text/javascript;base64,'+Buffer.from(mutant).toString('base64'));assert.notEqual(new ns[name](),ns[name]);
}else if(kind==='LDTHIS'){
 for(const receiver of [undefined,null,0,1,'x',false,{},()=>1]){assert.equal(Reflect.apply(a.f,receiver,[]),receiver);assert.equal(Reflect.apply(b.f,receiver,[]),receiver);++cases;}
}else if(kind==='LDFUNCTION'){
 assert.equal(a.f(),a.f);assert.equal(b.f(),b.f);assert.equal(Reflect.apply(b.f,{},[]),b.f);cases+=3;
 }else if(kind.includes('THISBY')){
 const write=kind.startsWith('ST');
 const run=f=>{const trace=[];const key={toString(){trace.push('key');return 'p';}};const args=kind.includes('BYVALUE')?[key]:[];const receiver={get p(){trace.push(['get',this===receiver]);return 7;},set p(v){trace.push(['set',this===receiver,v]);}};
   const result=Reflect.apply(f,receiver,args);assert.equal(result,write?receiver:7);
   for(const value of [null,undefined])assert.throws(()=>Reflect.apply(f,value,args),TypeError);
   const err=new RangeError('property');const bad={get p(){throw err;},set p(v){throw err;}};assert.throws(()=>Reflect.apply(f,bad,args),e=>e===err);
   if(write){const frozen=Object.freeze({p:2});assert.throws(()=>Reflect.apply(f,frozen,args),TypeError);assert.throws(()=>Reflect.apply(f,1,args),TypeError);}
   if(kind.includes('BYVALUE')){const keyError=new SyntaxError('key');const throwing={[Symbol.toPrimitive](){throw keyError;}};assert.throws(()=>Reflect.apply(f,{},[throwing]),e=>e===keyError);}

   return trace;};
 assert.deepEqual(run(a.f),run(b.f));cases+=write?10:8;
 const text=readFileSync(emitted,'utf8');const mutant=text.replace(kind.includes('BYVALUE')?/this\[[^\]]+\]/:'this.p','this.q');assert.notEqual(mutant,text);const m=await import('data:text/javascript;base64,'+Buffer.from(mutant).toString('base64'));assert.throws(()=>run(m.f));
 }else if(kind.startsWith('STOWNBY')||kind.startsWith('WIDE_STOWNBY')||kind==='CREATEITERRESULTOBJ_V8_V8'){
 const run=f=>{const x=f(),y=f();assert.notEqual(x,y);const expected=kind.startsWith('WIDE_STOWNBY')?[1]:kind.startsWith('STOWNBY')?{p:1}:{value:7,done:true};assert.deepEqual(x,expected);for(const key of Object.keys(expected))assert.deepEqual(Object.getOwnPropertyDescriptor(x,key),{value:expected[key],writable:true,enumerable:true,configurable:true});return x;};
 assert.deepEqual(run(a.f),run(b.f));cases+=6;
 if(kind!=='CREATEITERRESULTOBJ_V8_V8'){
   const proto=kind.startsWith('WIDE_STOWNBY')?Array.prototype:Object.prototype,key=kind.startsWith('WIDE_STOWNBY')?'0':'p';
   const saved=Object.getOwnPropertyDescriptor(proto,key);let writes=0;
   Object.defineProperty(proto,key,{set(v){++writes;},configurable:true});
   try{a.f();b.f();}finally{if(saved)Object.defineProperty(proto,key,saved);else delete proto[key];}
   assert.equal(writes,0);++cases;
 }
 }else if(kind==='WIDE_COPYRESTARGS_PREF_IMM16'){
 for(const xs of [[],[1],[1,2],[null,3,4]]){assert.deepEqual(a.f(...xs),xs.slice(1));assert.deepEqual(b.f(...xs),xs.slice(1));++cases;}assert.notEqual(b.f(1),b.f(1));++cases;
 }else if(kind==='THROW_UNDEFINEDIFHOLE_PREF_V8_V8'){
 assert.equal(a.f(),1);assert.equal(b.f(),1);cases+=2;
}else assert.fail('unknown fixture');
console.log(JSON.stringify({cases}));
