import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original,emitted]=process.argv.slice(2);
const load=async file=>import('data:text/javascript;base64,'+Buffer.from(readFileSync(file,'utf8')).toString('base64'));
const [a,b]=await Promise.all([load(original),load(emitted)]);let cases=0;
for(const name of Object.keys(a)) {
 for(const input of [[],[1,2],['x',null,undefined],Array(2),'abc']) {
  const run=fn=>{const trace=[];const source={ [Symbol.iterator](){trace.push('iterator');const i=input[Symbol.iterator]();return {next(){trace.push('next');return i.next();},return(){trace.push('return');return {done:true};}};}};
   try {const out=name==='wide'?fn({get p(){trace.push('p');}},source):fn(source);return {args:out.args,name:out.constructor.name,length:out.constructor.length,trace};}
   catch(e){return {error:e,trace};}};
  assert.deepEqual(run(b[name]),run(a[name]));++cases;
 }
 for(const stage of ['acquire','next','value']) {
  const run=fn=>{const trace=[];const source={ [Symbol.iterator](){trace.push('iterator');if(stage==='acquire')throw 11;
   return {next(){trace.push('next');if(stage==='next')throw 12;return {done:false,get value(){trace.push('value');throw 13;}};}};}};
   try{name==='wide'?fn({get p(){trace.push('p');}},source):fn(source);assert.fail('must throw');}
   catch(e){return {error:e,trace};}};
  assert.deepEqual(run(b[name]),run(a[name]));++cases;
 }
}
console.log(JSON.stringify({cases}));
