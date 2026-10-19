import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
const [engine, fixture] = process.argv.slice(2);
const temporary = mkdtempSync(join(tmpdir(), 'abcd-lexical-correspondence-'));
const run = (command, args) => {
  const p = spawnSync(command, args, {encoding:'utf8'});
  assert.equal(p.status, 0, p.stderr); return p.stdout;
};
try {
  const manifest = JSON.parse(readFileSync(join(fixture, 'manifest.json')));
  for (const [file, hash] of Object.entries(manifest.hashes))
    assert.equal(createHash('sha256').update(readFileSync(join(fixture, file))).digest('hex'), hash);
  const output = join(temporary, 'output');
  run(engine, [join(fixture, 'cells.abc'), output, '--value-source', '--language-source']);
  const record = JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status===0);
  assert.ok(record, 'the whole fixture must complete');
  const emitted = join(output, record.source);
  run(process.execPath, ['--check', emitted]);
  const original = join(temporary,'original.mjs');
  writeFileSync(original,readFileSync(join(fixture,'cells.js')));
  const runner = join(temporary,'run.mjs');
  writeFileSync(runner, `
import assert from 'node:assert/strict';
const m = await import(process.argv[2]); let trace=[]; let cases=0;
for (const name of ['make','permuted']) for(const initial of [null,0,false,'first'])
for(const mode of ['normal','throw-rhs','extra-throw','initial-throw','other-throw']) {
 const event=s=>trace.push(s); const seed={};
 for(const key of ['initial','extra','other']) Object.defineProperty(seed,key,{get(){
  event(key);if(mode===key+'-throw')throw new URIError();return key==='initial'?initial:key;
 }});
 try {
  const a=m[name](seed); const b=m[name]({initial:'isolated',extra:'B',other:'C'});
  assert.equal(a.read(),initial);
  for(const value of [1,'next',undefined,null,false,0]) {
   const rhs=()=>{event('rhs:'+String(value));assert.equal(a.read(),initial);if(mode==='throw-rhs')throw new RangeError();return value;};
   if(mode==='throw-rhs') {
    assert.throws(()=>a.write(rhs),RangeError);assert.equal(a.read(),initial);continue;
   }
   const children=a.write(rhs);event('read:'+String(a.read()));assert.equal(a.read(),value);
   assert.equal(b.read(),'isolated');
   for(const child of Array.isArray(children)?children:[children]) event('child:'+child());
   // Restore the live cell for the next RHS observation.
   a.write(()=>initial);
  }
 } catch(e){event('caught:'+e.constructor.name);}
 ++cases;
}
for(const throws of [false,true]) {
 const log=[];
 assert.throws(()=>m.early({before(write){
  log.push('before');write(()=>{log.push('rhs');if(throws)throw new SyntaxError();return 9;});
 },get extra(){log.push('WRONG extra');return 1;},get initial(){log.push('WRONG initial');return 2;}}),
 throws?SyntaxError:ReferenceError);
 assert.deepEqual(log,['before','rhs']);trace.push(log);++cases;
}
console.log(JSON.stringify({cases,trace}));`);
  const expected=run(process.execPath,[runner,original]);
  const actual=run(process.execPath,[runner,emitted]);
  assert.equal(actual,expected);assert.equal(JSON.parse(actual).cases,42);
  // Changing the incoming store target must be observed by live reads.
  const source=readFileSync(emitted,'utf8');
  const mutated=source.replace(/(\$local\d+) = (\$value\d+);/g, '$1 = "WRONG storage";');
  assert.notEqual(mutated,source,'mutation must alter an emitted store');
  const mutation=join(temporary,'mutation.mjs');writeFileSync(mutation,mutated);
  const observed=spawnSync(process.execPath,[runner,mutation],{encoding:'utf8'});
  assert.ok(observed.status!==0 || observed.stdout!==expected,'wrong store must be detected');
  console.log('42 engine/original cases: hoisted writers, inserted/permuted descendant demands, live writes, throws, TDZ and per-call isolation; wrong-store mutation detected');
} finally {rmSync(temporary,{recursive:true,force:true});}
