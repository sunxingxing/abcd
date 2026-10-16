// Syntax-only tree fixture: not engine-produced ForOfPlan/recovery acceptance.
import assert from 'node:assert/strict';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {execFileSync} from 'node:child_process';
const dir=await mkdtemp(join(tmpdir(),'abcd-for-of-tree-'));
try {
  const prefix=join(dir,'tree');
  execFileSync(process.argv[2],[prefix]);
  const functions={};
  for(const mode of ['continue','break','nested-continue','nested-break','sequence','async-name']) {
    const path=`${prefix}-${mode}.mjs`;
    execFileSync(process.execPath,['--check',path]);
    functions[mode]=(await import(pathToFileURL(path))).default;
  }
  for(const mode of ['continue','break']) {
    const run=functions[mode];const seen=[];
    assert.equal(run([],x=>seen.push(x)),17); // no undefined write on exhaustion
    assert.deepEqual(seen,[]);
    assert.equal(run([3,7,9],x=>seen.push(x)),mode==='break'?3:9);
    assert.deepEqual(seen,mode==='break'?[3]:[3,7,9]);
    const chars=[];run('a𝄞',x=>chars.push(x));
    assert.deepEqual(chars,mode==='break'?['a']:['a','𝄞']);
    const trace=[];
    const iterable={ [Symbol.iterator](){trace.push('acquire');let n=0;return {
      next(){trace.push('next');return n++<2?{value:n,done:false}:{done:true};},
      return(){trace.push('close');return {};}
    };}};
    assert.equal(run(iterable,x=>trace.push(x)),mode==='break'?1:2);
    assert.deepEqual(trace,mode==='break'?['acquire','next',1,'close']:['acquire','next',1,'next',2,'next']);
  }
  assert.equal(functions['async-name']([4,6],()=>{}),6);
  assert.equal(functions.sequence([4,6],()=>{}),6);
  for(const mode of ['nested-continue','nested-break']) {
    const seen=[];
    assert.equal(functions[mode]([3,7],x=>seen.push(x)),3);
    assert.deepEqual(seen,mode==='nested-break'?[3,3]:[3,3,7,3]);
  }
} finally {await rm(dir,{recursive:true,force:true});}
