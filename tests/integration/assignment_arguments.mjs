// Independent source oracle: compare receiver, [[Set]], key/RHS/call order and
// abrupt completion. ABC/source fixtures are not engine inputs to each other.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync, copyFileSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const [structure, fixtures, semantic] = process.argv.slice(2);
const directory = mkdtempSync(join(tmpdir(), 'abcd-assignment-'));
try {
  function evaluate(file) {
    const r = spawnSync(process.execPath, [join(fixtures, 'harness.mjs'), file], {encoding:'utf8'});
    assert.equal(r.status, 0, r.stderr); return r.stdout;
  }
  for (const name of ['normal','phased','multiple','computed']) {
    const original = join(directory, name+'.mjs');
    copyFileSync(join(fixtures, name+'.js'), original);
    const expected = evaluate(original);
    for (const flags of [['--language-numeric','--language-constructors'],['--language-source']]) {
      const output = join(directory, name+flags[0]);
      const r = spawnSync(structure,[join(fixtures,name+'.abc'),output,'--value-source',...flags],{encoding:'utf8'});
      assert.equal(r.status,0,r.stderr);
      const records = JSON.parse(readFileSync(join(output,'manifest.json'))).records.filter(r=>r.source_status!==2);
      assert.equal(records.length,1); assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
      const metadata=JSON.parse(readFileSync(join(output,records[0].metadata)));
      const method=metadata.methods.find(m=>m.runtime_name==='run');
      const instructions=spawnSync(semantic,[join(fixtures,name+'.abc'),String(method.entity),'--instructions'],{encoding:'utf8'});
      assert.equal(instructions.status,0,instructions.stderr);
      const ops=instructions.stdout.trim().split('\n').map(line=>JSON.parse(line));
      // Pinned dynamic ISA: STOBJBYNAME=67, STOBJBYVALUE=56. Assert [[Set]],
      // not STOWNBYNAME/[[Define]], actually occurs in the compiler fixture.
      const storeOpcode=name==='computed'?56:67;
      assert.equal(ops.filter(op=>op.opcode===storeOpcode).length,name==='multiple'?2:1);
      const file = join(output,records[0].source);
      assert.equal(evaluate(file),expected,name);
      const text = readFileSync(file,'utf8');
      assert.match(text, /globalThis\.obj\.f\(\(globalThis\.target/);
      if (name !== 'computed') {
        const mutated = join(directory,name+'-mutated.mjs');
        const replaced = text.replace(' = globalThis.rhs()', ' = (globalThis.rhs(), 999)');
        assert.notEqual(replaced,text); writeFileSync(mutated,replaced);
        assert.notEqual(evaluate(mutated),expected,'replaced RHS must fail');
        const detached = text.replace('globalThis.obj.f(', '(0, globalThis.obj.f)(');
        writeFileSync(mutated,detached);
        assert.notEqual(evaluate(mutated),expected,'detached receiver must fail');
      }
    }
  }
  // Inline computed key with a literal RHS still needs a closed occurrence /
  // reference proof. Preserve the refusal; do not skip key coercion to fuse.
  const output=join(directory,'computed-inline-refused');
  const refused=spawnSync(structure,[join(fixtures,'computed_inline.abc'),output,'--value-source','--language-source'],{encoding:'utf8'});
  assert.equal(refused.status,0,refused.stderr);
  const record=JSON.parse(readFileSync(join(output,'manifest.json'))).records.find(r=>r.source_status!==2);
  assert.equal(record.source_status,3);
  assert.equal(record.source_failure.call_reference.kind,1);
  assert.deepEqual(record.source_failure.causes.slice(0,4).map(c=>c.domain.split('::').at(-1)+'='+c.value),
    ['FunctionTreeError=1','ExpressionError=4','CallPlanError=4','CallReferenceError=1']);

} finally {rmSync(directory,{recursive:true,force:true});}
