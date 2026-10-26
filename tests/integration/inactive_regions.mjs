import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [structure, proof, semantic, fixtures] = process.argv.slice(2);
const root = mkdtempSync(join(tmpdir(), 'abcd-inactive-regions-'));
const run = (tool, args, status=0) => { const r=spawnSync(tool,args,{encoding:'utf8'});assert.equal(r.status,status,r.stdout+r.stderr);return r.stdout; };
const load=async(name,text)=>{const p=join(root,name+'.mjs');writeFileSync(p,text);return import(pathToFileURL(p).href);};
function observations(m) {
  const out=[];
  for(const x of [undefined,null,0,3,'x',false,true]) {out.push(m.passthrough(x));for(const flag of [false,true,0,1])out.push(m.split(x,flag));}
  for(const throwing of [false,true]) {
    const log=[];const input={get value(){log.push('get');if(throwing)throw new TypeError('ignored message');return 19;}};
    out.push([m.active(input),log]);
  }
  return out;
}
try {
  const expected=observations(await load('oracle',readFileSync(join(fixtures,'inactive.ts'),'utf8')));
  for(const mode of ['o0','default']) {
    const abc=join(fixtures,`inactive-${mode}.abc`);
    // Decode actual try tables and lift the actual instructions. Both compiler
    // O0 has two inactive regions and one reachable handler. The default
    // optimizer emits zero-size handler ranges, an explicitly refused extent.
    run(proof,[abc]);
    for(const flags of [[], ['--language-source']]) {
      const out=join(root,mode+flags[0]);run(structure,[abc,out,'--value-source',...flags]);
      const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
      assert.equal(records.length,1);
      if(mode==='default') {
        assert.equal(records[0].source_status,3);
        const failure=records[0].source_failure;
        assert.deepEqual(failure.causes.map(c=>[c.domain,c.value]),[
          ['abcd::source::PhasedMethodError',5],['abcd::source::ExceptionPlanError',2],['abcd::analysis::CatchRegionError',1]]);
        assert.ok(failure.exception_region);assert.ok(Number.isInteger(failure.method));
        const report=JSON.parse(run(semantic,[abc,String(failure.method),'--record-source','--value-source',...flags],1));
        assert.deepEqual(report.source_failure,failure);
        continue;
      }
      assert.equal(records[0].source_status,0,JSON.stringify(records));
      const text=readFileSync(join(out,records[0].source),'utf8');
      assert.equal((text.match(/catch \(/g)||[]).length,1);
      assert.deepEqual(observations(await load(mode+flags[0],text)),expected);
      const mutant=text.replace(/return (\$\w+)\.name;/,'throw $1;');assert.notEqual(mutant,text);
      const broken=await load('mutant'+mode+flags[0],mutant);
      assert.throws(()=>observations(broken),TypeError);
    }
  }
} finally {rmSync(root,{recursive:true,force:true});}
