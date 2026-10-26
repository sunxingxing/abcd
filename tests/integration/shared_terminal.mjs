// Only ABC is engine input. Independent source execution is the oracle.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, fixture] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-shared-terminal-'));
try {
  const runner = join(dir, 'run.mjs');
  writeFileSync(runner, `
const m = await import(process.argv[2]);
const runs = [], keys = ['first','equal','early','left','right','continuing','caught'];
for (const name of ['classify','dispatch','protectedDispatch'])
for (const a of [false,true]) for (const b of [false,true])
for (const kind of [0,1,2,3]) for (const changing of [false,true])
for (const fail of ['', ...keys.flatMap(k=>[k,'get:'+k])]) {
  const trace=[], hooks={}; let calls=0;
  for (const key of keys) Object.defineProperty(hooks,key,{get() {
    trace.push('get:'+key);
    if (fail==='get:'+key) throw new TypeError(key);
    return function(value) {
      if (this!==hooks) throw new Error('receiver');
      trace.push([key,value]);
      if (fail===key) throw new RangeError(key);
      if (key==='first') {
        calls++;
        return changing ? [a,b,!a,!b][(calls-1)%4] : Boolean(value);
      }
    };
  }});
  let result;
  try { result={returned: name==='classify' ? m[name](a,b,hooks) : m[name](kind,hooks)}; }
  catch(e) { result={thrown:e.constructor.name}; }
  finally { trace.push('caller-finally'); }
  runs.push({name,a,b,kind,changing,fail,result,trace});
}
console.log(JSON.stringify(runs));
`);
  const run = file => spawnSync(process.execPath, [runner, file], {encoding:'utf8', maxBuffer:16*1024*1024});
  const original = join(dir, 'original.mjs'); copyFileSync(join(fixture, 'shared.js'), original);
  const expected = run(original); assert.equal(expected.status, 0, expected.stderr);
  assert.equal(JSON.parse(expected.stdout).length, 1440);
  for (const [label, flags] of [['default',[]], ['source',['--language-source']]]) {
    const output = join(dir, label);
    const exported = spawnSync(structure, [join(fixture,'shared.abc'), output, '--value-source', ...flags], {encoding:'utf8'});
    assert.equal(exported.status, 0, exported.stderr);
    const records = JSON.parse(readFileSync(join(output,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length, 1); assert.equal(records[0].source_status, 0, JSON.stringify(records[0]));
    const source = join(output, records[0].source);
    const syntax = spawnSync(process.execPath, ['--check',source], {encoding:'utf8'});
    assert.equal(syntax.status,0,syntax.stderr);
    const actual = run(source); assert.equal(actual.status,0,actual.stderr); assert.equal(actual.stdout,expected.stdout,label);
    const text = readFileSync(source,'utf8');
    const mutations = [
      text.replaceAll('return "one";', 'void 0;'),
      text.replace(/(\$\w+\.early\(\);)/g, '$1 $1'),
      text.replaceAll('.left();', '.right();'),
      text.replaceAll('.continuing();', '.early();'),
      text.replace(/\.caught\(\$\w+\.name\)/g, '.caught("wrong")'),
    ];
    for (const [i,changed] of mutations.entries()) {
      assert.notEqual(changed,text,'mutation must apply: '+i);
      const mutant = join(dir,label+'-'+i+'.mjs'); writeFileSync(mutant,changed);
      const observed = run(mutant);
      assert.ok(observed.status!==0 || observed.stdout!==expected.stdout,'undetected mutation: '+i);
    }
  }
} finally { rmSync(dir,{recursive:true,force:true}); }
