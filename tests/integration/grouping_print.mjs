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

{const text=execFileSync(process.argv[2],['rebind'],{encoding:'utf8'});
 const call=Function('"use strict";return '+text)(),seen=[];
 function original(value){seen.push(this);return value;}
 const replacement=()=>{throw Error('callee re-read');};
 assert.equal(call(original,replacement,0),replacement);assert.deepEqual(seen,[undefined]);}

{const text=execFileSync(process.argv[2],['evalalias'],{encoding:'utf8'});
 const call=Function('"use strict";return '+text)();
 assert.equal(call(eval,false,17),'undefined');} // Alias remains indirect eval.

{const text=execFileSync(process.argv[2],['voidconstruct'],{encoding:'utf8'});
 const call=Function('return '+text)();function C(...args){return args;}
 assert.deepEqual(call(C),[undefined,{}]);assert(text.includes('new f(void 0, {})'));}
for(const form of ['awaitreturn','awaitmember','awaitarg','awaitassign']) {
 const text=execFileSync(process.argv[2],[form],{encoding:'utf8'}),f=Function('return '+text)();
 const log=[];const promise={then(resolve){log.push('await');resolve(form==='awaitmember'?{p:23}:23);}};
 assert.equal(await f(x=>{log.push('call');return x;},promise,0),23);
 assert.deepEqual(log,form==='awaitarg'?['await','call']:['await']);
}
for(const [form,oracle]of [['negated',(a,b)=>a==b],['strictnegated',(a,b)=>a===b],['negatedless',(a,b)=>!(a<b)]]) {
 const text=execFileSync(process.argv[2],[form],{encoding:'utf8'}),f=Function('return '+text)();
 for(const a of [null,undefined,0,-0,NaN,1,1n,'1',false,{},[]])for(const b of [null,undefined,0,NaN,1,1n,'1',false,{}])assert.equal(f(null,a,b),oracle(a,b));
 const run=fn=>{const log=[];try{const a={valueOf(){log.push('left');return 1;}};log.push(fn(a,2));}catch(e){log.push(e.constructor.name);}return log;};
 assert.deepEqual(run((a,b)=>f(null,a,b)),run(oracle));
 const thrown=()=>{throw new RangeError('valueOf');};assert.deepEqual(run((a,b)=>f(null,{valueOf:thrown},b)),run((a,b)=>oracle({valueOf:thrown},b)));
}
console.log('await contexts and equality complements: coercions, NaN, BigInt and effects passed');
