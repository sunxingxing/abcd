import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {parseJavaScript} from './class_reference.mjs';
const rows=execFileSync(process.argv[2],['--emit'],{encoding:'utf8'}).trim().split('\n').map(JSON.parse);
const failure=Symbol('failure');
const cases=[];
for(const base of ['object','null','undefined','primitive','proxy','setter','getter'])
  for(const key of ['string','number','coerce','throw','getThrow','returnObject'])cases.push({base,key});
function run(fn,scenario) {
  const trace=[];
  let obj=scenario.base==='null'?null:scenario.base==='undefined'?undefined:scenario.base==='primitive'?1:{};
  if(scenario.base==='proxy')obj=new Proxy(obj,{get(target,key){trace.push(['get',String(key)]);throw failure;},set(target,key,value){trace.push(['set',String(key),value]);throw failure;}});
  if(scenario.base==='setter')Object.defineProperty(obj,'fixed',{set(value){trace.push(['setter',value]);throw failure;}});
  if(scenario.base==='getter')Object.defineProperty(obj,'fixed',{get(){trace.push('getter');throw failure;}});
  let key=scenario.key==='string'?'fixed':scenario.key==='number'?3:{[Symbol.toPrimitive](hint){trace.push(['key',hint]);if(scenario.key==='throw')throw failure;return scenario.key==='returnObject'?{}:'fixed';}};
  if(scenario.key==='getThrow')key=Object.defineProperty({},Symbol.toPrimitive,{get(){trace.push('key getter');throw failure;}});
  try {return {trace,value:fn(obj,key,9)};}catch(error){return {trace,error:error===failure?'failure':error.name};}
}
let comparisons=0,mutants=0;
for(const row of rows) {
  parseJavaScript('('+row.source+')');
  const generated=Function('return ('+row.source+')')();
  const reference=Function('obj','key','rhs','"use strict";let state=7;try {'+
    (row.read?(row.computed?'obj[key];':'obj.fixed;')+'state=rhs;':(row.computed?'obj[key]':'obj.fixed')+'=(state=rhs);')+
    '}catch(error){return state;}return state;');
  for(const scenario of cases){assert.deepEqual(run(generated,scenario),run(reference,scenario),JSON.stringify({row,scenario}));++comparisons;}
  if(row.computed&&!row.read) {
    // Move conversion before the RHS write, or reject a null base before the
    // key/RHS. Both alter caught state or user-hook traces.
    for(const body of ['const converted=typeof key==="symbol"?key:String(key);obj[converted]=(state=rhs);',
        'if(obj===null||obj===undefined)throw new TypeError();obj[key]=(state=rhs);',
        'obj[key];obj[key]=(state=rhs);']) {
      const mutant=Function('obj','key','rhs','"use strict";let state=7;try{'+body+'}catch(error){return state;}return state;');
      assert.ok(cases.some(scenario=>{try{assert.deepEqual(run(mutant,scenario),run(reference,scenario));return false;}catch{return true;}}));++mutants;
    }
  }
}
console.log(`${rows.length} property sources / ${comparisons} independent execution comparisons / ${mutants} adverse mutants`);
