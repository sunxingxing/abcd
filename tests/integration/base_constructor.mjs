import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const source=await import(pathToFileURL(process.argv[2]));
for(const C of Object.values(source)) {
  assert.equal(C.length,1);
  assert.equal(C.name,'Recovered');
  assert.throws(()=>C(),TypeError);
  assert.equal(Object.getOwnPropertyDescriptor(C,'prototype').writable,false);
}
for(const value of [undefined,null,0,42,'text',true,Symbol('value'),1n,{},()=>{}]) {
  const instance=new source.parameter(value);
  if(value!==null && (typeof value==='object' || typeof value==='function')) assert.equal(instance,value);
  else assert.equal(Object.getPrototypeOf(instance),source.parameter.prototype);
  assert.throws(()=>new source.thrown(value),error=>Object.is(error,value));
}
assert.equal(new source.self(),source.self);
assert.equal(new source.newTarget(),source.newTarget);
assert.equal(Object.getPrototypeOf(new source.receiver()),source.receiver.prototype);
function Other() {}
assert.equal(Reflect.construct(source.newTarget,[],Other),Other);
assert.equal(Object.getPrototypeOf(Reflect.construct(source.receiver,[],Other)),Other.prototype);
const calls=[];
Object.defineProperty(source.assigned.prototype,'x',{
  configurable:true,
  set(value){calls.push({receiver:this,value});}
});
const marker=Object.freeze({marker:true});
const assigned=new source.assigned(marker);
assert.equal(Object.hasOwn(assigned,'x'),false);
assert.deepEqual(calls.map(c=>c.value),[marker,undefined]);
assert.ok(calls.every(c=>c.receiver===assigned));
Object.defineProperty(source.assigned.prototype,'x',{set(){throw marker;}});
assert.throws(()=>new source.assigned(0),error=>error===marker);
