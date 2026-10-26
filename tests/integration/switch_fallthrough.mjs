// The engine receives only the ABC. Source is an independent execution oracle.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,copyFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure,fixture]=process.argv.slice(2),dir=mkdtempSync(join(tmpdir(),'abcd-switch-fallthrough-'));
try {
  const runner=join(dir,'run.mjs');writeFileSync(runner,`
const m=await import(process.argv[2]),runs=[];
for(const name of ['dispatch','three','middle','empty','exits','loop','ordered'])
for(const k of [-1,0,1,2,3,4])for(const value of [false,true,0,1,null])
for(const fail of ['', 'kind','value','lookup:first','lookup:second','lookup:third','lookup:other','lookup:end','lookup:test','call:test','call:first','call:second','call:third','call:other','call:end']) {
 const trace=[],hooks={},message={},keys=['first','second','third','other','end'];
 const get=(k,v)=>{trace.push('get:'+k);if(fail===k)throw new TypeError(k);return v;};
 Object.defineProperty(message,'kind',{get(){return get('kind',name==='ordered'?k:(keys[k]??'unknown'));}});
 Object.defineProperty(message,'value',{get(){return get('value',value);}});
 Object.defineProperty(hooks,'value',{get(){return get('value',value);}});
 for(const key of keys)Object.defineProperty(hooks,key,{get(){get('lookup:'+key);return function(x){if(this!==hooks)throw new Error('receiver');trace.push([key,x]);if(fail==='call:'+key)throw new RangeError(key);return key;};}});
 Object.defineProperty(hooks,'test',{get(){get('lookup:test');return function(i){if(this!==hooks)throw new Error('receiver');trace.push(['test',i]);if(fail==='call:test')throw new RangeError('test');return i;};}});
 hooks.kind=function(i){if(this!==hooks)throw new Error('receiver');trace.push(['kind',i]);return (i+k)%4;};
 let result;try{result={returned:(name==='dispatch'||name==='ordered')?m[name](message,hooks):m[name](name==='loop'?k+1:k,hooks)};}catch(e){result={thrown:e.constructor.name};}
 runs.push({name,k,value,fail,trace,result});
}
console.log(JSON.stringify(runs));
`);
  const run=path=>spawnSync(process.execPath,[runner,path],{encoding:'utf8',maxBuffer:16*1024*1024});
  const source=join(dir,'original.mjs');copyFileSync(join(fixture,'switch_fallthrough.js'),source);
  const expected=run(source);assert.equal(expected.status,0,expected.stderr);assert.equal(JSON.parse(expected.stdout).length,3150);
  for(const [name,flags] of [['default',[]],['source',['--language-source']]]) {
    const out=join(dir,name),exported=spawnSync(structure,[join(fixture,'switch_fallthrough.abc'),out,'--value-source',...flags],{encoding:'utf8'});
    assert.equal(exported.status,0,exported.stderr);
    const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);assert.equal(records.length,1);
    assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const file=join(out,records[0].source),text=readFileSync(file,'utf8');assert.ok(text.includes('switch ('));
    const syntax=spawnSync(process.execPath,['--check',file],{encoding:'utf8'});assert.equal(syntax.status,0,syntax.stderr);
    const actual=run(file);assert.equal(actual.status,0,actual.stderr);const a=JSON.parse(actual.stdout),e=JSON.parse(expected.stdout);assert.equal(a.length,e.length);
    for(let j=0;j<a.length;++j)assert.deepEqual(a[j],e[j],name+' scenario '+j);
    for(const [i,mutant] of [text.replace('case "third":','case "lost":'),text.replaceAll('.third(','.second(')].entries()) {
      assert.notEqual(mutant,text);const path=join(dir,name+'-'+i+'.mjs');writeFileSync(path,mutant);const observed=run(path);assert.ok(observed.status!==0||observed.stdout!==expected.stdout,'mutation '+i);
    }
  }
  console.log('switch_fallthrough: 3150 independent scenarios per contract; observed case/order mutations');
}finally{rmSync(dir,{recursive:true,force:true});}
