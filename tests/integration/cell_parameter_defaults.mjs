import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure, semantic, abc] = process.argv.slice(2);
const dir=mkdtempSync(join(tmpdir(),'abcd-cell-parameter-defaults-'));
const run=(exe,args,status=0)=>{const p=spawnSync(exe,args,{encoding:'utf8',timeout:30000});assert.equal(p.status,status,p.stderr+'\n'+p.stdout);return p.stdout;};
try {
  for(const [label,flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const out=join(dir,label);run(structure,[abc,out,'--value-source',...flags]);
    const records=JSON.parse(readFileSync(join(out,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1);const record=records[0];assert.equal(record.source_status,3);
    const f=record.source_failure;assert.deepEqual(f.causes,[{domain:'abcd::source::FunctionAbiError',value:7}]);
    const d=f.cell_parameter_default;assert.equal(d.verified,true);assert.equal(d.count,1);assert.equal(d.parameter,2);
    assert.equal(d.local.slot,0);assert.equal(d.outer.slot,0);assert.equal(d.outer.depth,0); // incoming depth, not instruction depth
    const instructions=run(semantic,[abc,String(d.location.method),'--instructions']).trim().split('\n').map(JSON.parse);
    const at=location=>instructions.find(i=>i.pc===location.pc);
    // Pinned ISA encodings: local store/load and named hole guard. These are
    // fixture assertions, never production admission rules.
    assert.equal(at(d.initial_store).opcode,0x3d);assert.equal(at(d.initial_store).operands[0].bits,0);
    assert.equal(at(d.test).opcode,0x3c);assert.equal(at(d.test).operands[0].bits,0);
    assert.equal(at(d.read).opcode,0x3c);assert.equal(at(d.read).operands[0].bits,1);
    assert.equal(at(d.guard).opcode,0x09fe);assert.equal(at(d.guard).exception_edge.propagates_to_caller,true);
    assert.equal(at(d.fallback_store).opcode,0x3d);assert.equal(at(d.fallback_store).operands[0].bits,0);
    const report=JSON.parse(run(semantic,[abc,String(d.location.method),'--record-source',...flags],1));
    // Both CLI entry points publish the same typed evidence, not a source file.
    const find=x=>x&&typeof x==='object'?(x.cell_parameter_default??x.source_cell_parameter_default??Object.values(x).map(find).find(Boolean)):undefined;
    assert.deepEqual(find(report),d);
    assert.equal(record.source,undefined);
  }
  console.log('cell_parameter_defaults_cli_test: both contracts retain kLength; verified original-IR evidence and CLI consistency');
} finally {rmSync(dir,{recursive:true,force:true});}
