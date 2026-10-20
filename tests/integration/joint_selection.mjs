import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
const [engine,fixture]=process.argv.slice(2);
const temp=mkdtempSync(join(tmpdir(),'abcd-joint-selection-'));
const run=(exe,args)=>{const result=spawnSync(exe,args,{encoding:'utf8'});assert.equal(result.status,0,result.stderr);return result.stdout;};
try {
  const manifest=JSON.parse(readFileSync(join(fixture,'manifest.json')));
  for(const [name,sha]of Object.entries(manifest.hashes))assert.equal(createHash('sha256').update(readFileSync(join(fixture,name))).digest('hex'),sha);
  const runner=join(temp,'run.mjs');
  writeFileSync(runner,`
const m=await import(process.argv[2]);const name=process.argv[3];const trace=[];let count=0;
const sentinel={get valueOf(){trace.push('unexpected-valueOf');throw new TypeError();}};
const values=[null,undefined,false,0,-0,NaN,'',1,'yes',sentinel];
const encode=value=>value===undefined?['undefined']:typeof value==='number'&&Number.isNaN(value)?['NaN']:Object.is(value,-0)?['-0']:value!==null&&typeof value==='object'?['identity',values.indexOf(value)]:value;
if(m[name].length!==({booleanJoin:4,booleanNot:3,booleanConjunction:2})[name]||m[name].name!==name)throw new Error('function ABI');
for(const x of values)for(const y of values)for(const z of values)
for(const mode of ['normal','callee-throw','noncallable','a-throw','b-throw','c-throw','replace']) {
 const object={};
 Object.defineProperty(object,'invoke',{configurable:true,get(){trace.push('callee');if(mode==='callee-throw')throw new RangeError();if(mode==='noncallable')return 0;
  return function(value){trace.push(['invoke',this===object,encode(value)]);return value;};}});
 const input={};for(const [key,value]of [['a',x],['b',y],['c',z]])Object.defineProperty(input,key,{get(){trace.push(key);if(mode===key+'-throw')throw new SyntaxError();
  if(mode==='replace')Object.defineProperty(object,'invoke',{configurable:true,value(){trace.push('wrong-callee');}});return value;}});
 try{const result=name==='booleanConjunction'||name==='booleanNormalized'?m[name](object,input):name==='booleanNot'?m[name](object,x,y):m[name](object,x,y,z);trace.push(['result',encode(result)]);}
 catch(e){trace.push(['error',e.constructor.name]);}++count;
}
console.log(JSON.stringify({count,trace}));`);
  let scenarios=0,refused=0;
  for(const entry of manifest.fixtures) {
    const output=join(temp,entry.abc);run(engine,[join(fixture,entry.abc),output,'--value-source','--language-source']);
    const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status!==2);
    const publish=entry.mode==='O0'&&['booleanJoin','booleanNot','booleanConjunction'].includes(entry.name);
    if(!publish) {
      assert.equal(record.source_status,3,entry.abc);assert.equal(record.source_failure?.call_reference?.kind,1,entry.abc);++refused;continue;
    }
    assert.equal(record.source_status,0,JSON.stringify(record));const source=join(output,record.source);run(process.execPath,['--check',source]);
    const original=join(temp,entry.name+'.mjs');writeFileSync(original,readFileSync(join(fixture,entry.source)));
    const expected=run(process.execPath,[runner,original,entry.name]);assert.equal(run(process.execPath,[runner,source,entry.name]),expected,entry.abc);
    scenarios+=JSON.parse(expected).count;
    const text=readFileSync(source,'utf8');assert.match(text,/\$arg0\.invoke\(/);assert.ok(!text.includes('.call(')&&!text.includes('.apply('));
    const wrong=join(temp,entry.name+'-wrong.mjs');
    writeFileSync(wrong,text.replaceAll('$arg0.invoke(','(0, $arg0.invoke)('));assert.notEqual(run(process.execPath,[runner,wrong,entry.name]),expected);
    if(entry.name==='booleanJoin'||entry.name==='booleanConjunction') {
      assert.ok(text.includes(' && '));writeFileSync(wrong,text.replace(' && ',' || '));assert.notEqual(run(process.execPath,[runner,wrong,entry.name]),expected);
    }
  }
  assert.equal(scenarios,21000);assert.equal(refused,11);
  console.log(scenarios+' engine-output/original scenarios; 3 published O0 fixtures; 11 precise remaining reference refusals; receiver/operator mutations observed');
} finally {rmSync(temp,{recursive:true,force:true});}
