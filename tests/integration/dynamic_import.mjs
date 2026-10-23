import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const [original,emitted]=process.argv.slice(2);
const load=p=>import('data:text/javascript;base64,'+Buffer.from(readFileSync(p)).toString('base64'));
const a=await load(original),b=await load(emitted);let cases=0;
const module='data:text/javascript,export const value=17;export default 23;';
for(const mode of ['success','missing','getter-throw','after-throw','symbol','coercion','coercion-throw']) {
 const run=async m=>{
  const trace=[];let specifier=mode==='missing'?'data:text/javascript,throw new Error("load")':module;
  if(mode==='symbol')specifier=Symbol('specifier');
  if(mode.startsWith('coercion'))specifier={toString(){trace.push('coerce');if(mode==='coercion-throw')throw new RangeError('specifier');return module;}};
  const input={get specifier(){trace.push('get');if(mode==='getter-throw')throw new TypeError('get');return specifier;},after(){trace.push('after');if(mode==='after-throw')throw new SyntaxError('after');}};
  try { const p=m.observe?m.observe(input):m.run(specifier);trace.push('returned');const ns=await p;return {trace,result:[ns.value,ns.default]};}
  catch(e){return {trace,error:[e.name,e.message]};}
 };
 assert.deepEqual(await run(b),await run(a),mode);cases++;
}
for(const m of [a,b]) {
 const first=m.run(module),second=m.run(module);assert.notEqual(first,second);
 const [x,y]=await Promise.all([first,second]);assert.equal(x,y);assert.equal(x.value,17);
}
cases++;
console.log(JSON.stringify({cases,exclusions:['ark-dynamic-import-coercion-timing','ark-native-cjs-namespace']}));
