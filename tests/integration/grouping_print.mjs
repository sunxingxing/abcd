import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
for(const form of ['value','member','base','sign','updatebase','objectstatement','objectvalue']){
 const text=execFileSync(process.argv[2],[form],{encoding:'utf8'});
 const f=Function('"use strict"; return '+text)();
 if(form.startsWith('object')){
   assert.equal(f(null,false,23),23);
 }else if(form==='sign'||form==='updatebase'){
   assert.deepEqual(f(x=>x,true,3),{p:form==='sign'?4:undefined});
 }else if(form==='value'){
   const trace=[];function target(...args){trace.push([this,args]);return args;}
   // Rest parameters cannot carry an explicit strict directive; enclosing
   // module code already supplies strictness for this ordinary call oracle.
   assert.deepEqual(f(target,true,3),[{p:3}]);assert.deepEqual(f(target,false,3),[{}]);
   assert.equal(trace[0][0],undefined);assert.equal(trace[1][0],undefined);
 }else if(form==='member'){
   const object={push(value){assert.equal(this,object);return value;}};
   assert.deepEqual(f(object,true,4),{p:4});assert.deepEqual(f(object,false,4),{});
 }else{assert.equal(f(null,true,4),1);assert.equal(f(null,false,4),1);}
}
console.log('grouping print differential: seven forms, both branches passed');
