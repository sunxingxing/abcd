// The engine reads only the ABC. Original JS is an independent execution oracle.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixture] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-switch-optional-'));
try {
  const runner = join(dir,'run.mjs');
  writeFileSync(runner, `
const m=await import(process.argv[2]), runs=[];
for(const name of ['dispatch','chained']) for(const kind of [-1,0,1,2,3])
for(const value of ['undefined','null','object','changing']) for(const count of [0,2])
for(const fail of ['', 'value','pause','resume','call:pause','call:resume','count','item','other','end']) {
  const trace=[], hooks={}, receiver={}; let reads=0;
  for(const key of ['pause','resume'])Object.defineProperty(receiver,key,{get(){
    trace.push('get:'+key);if(fail===key)throw new TypeError(key);
    return function(){if(this!==receiver)throw new Error('receiver');trace.push('call:'+key);
      if(fail==='call:'+key)throw new RangeError(key);};
  }});
  const state={get value(){trace.push('value');reads++;if(fail==='value')throw new SyntaxError('value');
    if(value==='null')return null;if(value==='undefined')return undefined;
    if(value==='changing'&&reads>1)return null;return receiver;}};
  Object.defineProperty(hooks,'count',{get(){trace.push('count');if(fail==='count')throw new TypeError('count');return count;}});
  for(const key of ['item','other','end'])hooks[key]=function(x){if(this!==hooks)throw new Error('hook receiver');
    trace.push([key,x]);if(fail===key)throw new ReferenceError(key);return 'result';};
  let result;try{result={returned:m[name](kind,state,hooks)};}catch(e){result={thrown:e.constructor.name};}
  runs.push({name,kind,value,count,fail,trace,result});
}
console.log(JSON.stringify(runs));
`);
  const run=file=>spawnSync(process.execPath,[runner,file],{encoding:'utf8',maxBuffer:16*1024*1024});
  const original=join(dir,'original.mjs');copyFileSync(join(fixture,'switch_optional.js'),original);
  const expected=run(original);assert.equal(expected.status,0,expected.stderr);
  assert.equal(JSON.parse(expected.stdout).length,800);
  for(const [name,flags] of [['previous',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(dir,name);
    const exported=spawnSync(structure,[join(fixture,'switch_optional.abc'),out,'--value-source',...flags],{encoding:'utf8'});
    assert.equal(exported.status,0,exported.stderr);
    const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1);assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const file=join(out,records[0].source), text=readFileSync(file,'utf8');
    const syntax=spawnSync(process.execPath,['--check',file],{encoding:'utf8'});assert.equal(syntax.status,0,syntax.stderr);
    const actual=run(file);assert.equal(actual.status,0,actual.stderr);assert.equal(actual.stdout,expected.stdout,name);
    for(const [i,mutant] of [text.replaceAll('?.pause()','.pause()'),
        text.replaceAll('?.pause()','?.resume()'),text.replaceAll('.end()','.other()'),
        text.replaceAll('?.resume()','?.pause()')].entries()) {
      assert.notEqual(mutant,text,'mutation applies '+i);
      const path=join(dir,name+'-'+i+'.mjs');writeFileSync(path,mutant);
      const observed=run(path);assert.ok(observed.status!==0||observed.stdout!==expected.stdout,'mutation '+i);
    }
  }
  console.log('switch_optional: 800 independent scenarios per contract, four observed mutations');
} finally {rmSync(dir,{recursive:true,force:true});}
