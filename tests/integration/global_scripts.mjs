import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const [original,emitted,kind]=process.argv.slice(2);
const evaluate=(file,oracle=false)=>{
 const context=vm.createContext({});let error,completion;
 // The SDK entry has an explicit RETURNUNDEFINED. The evaluation-only
 // oracle therefore ends with that completion; the emitted script must supply
 // it itself. The original source is never an engine input.
 try{completion=new vm.Script(readFileSync(file,'utf8')+(oracle?'\nvoid 0;':''),{filename:'input.js'}).runInContext(context);}
 catch(e){error={name:e.name,message:e.message};}
 const observation=context.scriptObservation===undefined?null:JSON.parse(JSON.stringify(context.scriptObservation));
 return {observation,error,context,completion};
};
const old=evaluate(original,true),now=evaluate(emitted);let cases=1;
if(kind==='sloppy'){
 assert.equal(old.context.missingGlobalForScript,4);
 assert.equal(now.error?.name,'ReferenceError'); // USER-APPROVED Ark checked-write difference.
 assert.equal(now.context.missingGlobalForScript,undefined);
}else{
 assert.deepEqual(now.observation,old.observation);assert.deepEqual(now.error,old.error);assert.equal(now.completion,old.completion);
 if(kind==='lexicals')assert.deepEqual(now.observation,[6,3,3,7,3]);
 if(kind==='bindings'){
  assert.deepEqual(now.observation,[6,2,3,3,7,3]);
  for(const method of ['twice','bump']){assert.equal(now.context[method].name,old.context[method].name);assert.equal(now.context[method].length,old.context[method].length);++cases;}
  for(const n of [-2,0,7,15]){assert.equal(now.context.bump(n),old.context.bump(n));++cases;}
 }
 if(kind==='errors'||kind==='lexical_errors')for(const expression of ['constWrite(7)','missingWrite(8)','missingRead()']){
  const run=context=>{try{vm.runInContext(expression,context);return null;}catch(e){return [e.name,e.message];}};
  assert.deepEqual(run(now.context),run(old.context));++cases;
 }
 if(kind==='closures'||kind==='lexical_closures'){assert.deepEqual(now.observation,[9,9,true]);++cases;}
}
console.log(JSON.stringify({cases,exclusions:kind==='sloppy'?['ark-sloppy-unresolved-write']:[]}));
