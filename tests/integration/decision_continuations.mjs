// Bytecode is the engine input; the independently executed source is oracle-only.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixture] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-decision-continuation-'));
try {
  const runner = join(dir, 'run.mjs');
  writeFileSync(runner, `
const m = await import(process.argv[2]);
const final = await import(process.argv[3]);
const runs = [];
const keys = ['first', 'second', 'left', 'right', 'terminal', 'continuing', 'tail', 'caught', 'body', 'finally'];
for (const name of ['run', 'protectedRun', 'asyncRun'])
for (const wrapped of [false,true])
for (const outer of [false,true]) for (const choice of [false,true])
for (const first of [false,true]) for (const second of [false,true])
for (const fail of ['', ...keys.flatMap(key => [key,'get:'+key])]) {
  const trace = [], hooks = {};
  for (const key of keys) Object.defineProperty(hooks,key,{get() {
    trace.push('get:'+key);
    if (fail === 'get:'+key) throw new TypeError(key);
    return function(...args) {
      if (this !== hooks) throw new Error('receiver');
      trace.push([key,...args]);
      if (fail === key) throw new RangeError(key);
      if (key === 'first') return first;
      if (key === 'second') return second;
      if (key === 'body') return m[name](outer,choice,hooks);
    };
  }});
  let result;
  try {
    result = {returned:await (wrapped ? final.finallyRun(outer,choice,hooks) : m[name](outer,choice,hooks))};
  } catch(e) { result = {thrown:e.constructor.name}; }
  finally { trace.push('caller-finally'); }
  runs.push({name,wrapped,outer,choice,first,second,fail,result,trace});
}
console.log(JSON.stringify(runs));
`);
  const run = (source, final) => spawnSync(process.execPath,[runner,source,final],{encoding:'utf8',maxBuffer:16*1024*1024});
  const originals = ['decision','finally'].map(name => {
    const path=join(dir,name+'.mjs'); copyFileSync(join(fixture,name+'.js'),path); return path;
  });
  const expected=run(...originals); assert.equal(expected.status,0,expected.stderr);
  assert.equal(JSON.parse(expected.stdout).length,2016);
  for (const [label,flags] of [['default',[]],['source',['--language-source']]]) {
    const files = ['decision','finally'].map(name => {
      const dest=join(dir,label+'-'+name);
      const exported=spawnSync(structure,[join(fixture,name+'.abc'),dest,'--value-source',...flags],{encoding:'utf8'});
      assert.equal(exported.status,0,exported.stderr);
      const records=JSON.parse(readFileSync(join(dest,'manifest.json'))).records.filter(r=>r.source_status!==2);
      assert.equal(records.length,1); assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
      return join(dest,records[0].source);
    });
    const actual=run(...files); assert.equal(actual.status,0,actual.stderr); assert.equal(actual.stdout,expected.stdout,label);
    const text=readFileSync(files[0],'utf8');
    const mutations=[
      text.replaceAll('return "early";','void 0;'),
      text.replace(/(\$\w+\.continuing\(\);)/g,'$1 $1'),
      text.replace(/!(\$\w+\.first\(\)) \|\| !(\$\w+\.second\(\))/g,'!($1 & $2)'), // Eager second condition.
      text.replace(/(\$\w+)\.tail\(\);/g,'if (false) $1.tail();'),
      text.replaceAll('.left();','.right();'),
      text.replace(/\.caught\(\$\w+\.name\)/g,'.caught("wrong")'),
    ];
    for (const [i,changed] of mutations.entries()) {
      assert.notEqual(changed,text,'mutation must apply: '+i);
      const mutant=join(dir,label+'-'+i+'.mjs'); writeFileSync(mutant,changed);
      const result=run(mutant,files[1]);
      assert.ok(result.status!==0 || result.stdout!==expected.stdout,'undetected mutation: '+i);
    }
    const finalText=readFileSync(files[1],'utf8');
    const changedFinal=finalText.replace(/\$\w+\["finally"\]\(\);/g,'void 0;');
    assert.notEqual(changedFinal,finalText,'finally mutation must apply');
    const finalMutant=join(dir,label+'-finally.mjs'); writeFileSync(finalMutant,changedFinal);
    const result=run(files[0],finalMutant);
    assert.ok(result.status!==0 || result.stdout!==expected.stdout,'undetected finally mutation');
  }
} finally { rmSync(dir,{recursive:true,force:true}); }
