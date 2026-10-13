// Engine-generated source vs independent JS oracle. No message/stack equality.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, copyFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const [structure, semantic, fixtures] = process.argv.slice(2);
const dir = mkdtempSync(join(tmpdir(), 'abcd-guarded-calls-'));
try {
  const runner = join(dir, 'run.mjs');
  writeFileSync(runner, `
const mode = process.argv[3];
globalThis.trace = [];
globalThis.state = 7;
const record = value => trace.push(value);
const receiver = {tag:'original'};
Object.defineProperty(receiver, 'method', {configurable:true, get() {
  record('getter'); if (mode === 'getter') throw new RangeError();
  return function(...args) {
    record(['call',this.tag,...args]); if(mode==='call') throw new SyntaxError(); return args[1];
  };
}});
globalThis.receiver = receiver;
globalThis.before = () => {
  record('before'); if (mode === 'before') throw new TypeError();
  if (mode === 'rewrite') Object.defineProperty(receiver, 'method', {value: function(){record('replacement');return -1;}});
  if (mode === 'receiver') globalThis.receiver = {tag:'new', method(){record('new receiver');return -2;}};
  return 'a';
};
globalThis.after = () => {record('after');if(mode==='after')throw new URIError();return 'z';};
const m = await import(process.argv[2]);
console.log(JSON.stringify({early:m.early,late:m.late,trace}));
`);
  const run = (file, mode) => spawnSync(process.execPath, [runner, file, mode], {encoding:'utf8'});
  for (const [label, flags] of [['old',['--language-numeric','--language-constructors']],['source',['--language-source']]]) {
    const output = join(dir, label);
    const exported = spawnSync(structure, [join(fixtures,'calls.abc'),output,'--value-source',...flags],{encoding:'utf8'});
    assert.equal(exported.status,0,exported.stderr);
    const records = JSON.parse(readFileSync(join(output,'manifest.json'))).records.filter(r=>r.source_status!==2);
    assert.equal(records.length,1); assert.equal(records[0].source_status,0,JSON.stringify(records[0]));
    const metadata=JSON.parse(readFileSync(join(output,records[0].metadata)));
    const method=metadata.methods.find(m=>m.runtime_name==='run'); assert.ok(method);
    const disassembled=spawnSync(semantic,[join(fixtures,'calls.abc'),String(method.entity),'--instructions'],{encoding:'utf8'});
    assert.equal(disassembled.status,0,disassembled.stderr);
    const instructions=disassembled.stdout.trim().split('\n').map(line=>JSON.parse(line));
    // Encodings from the pinned dynamic ISA: LDLEXVAR_IMM4_IMM4,
    // THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16, CALLTHIS3_IMM8_V8_V8_V8_V8.
    const opcode={load:0x3c,guard:0x09fe,member:0x30};
    const guard=instructions.find(i=>i.opcode===opcode.guard);
    const member=instructions.find(i=>i.opcode===opcode.member);
    assert.ok(instructions.some(i=>i.opcode===opcode.load));
    assert.ok(guard?.exception_edge?.handler_pc!==undefined);
    assert.equal(member?.exception_edge?.handler_pc,guard.exception_edge.handler_pc);
    const original = join(dir,label+'-oracle.mjs'); copyFileSync(join(fixtures,'calls.js'),original);
    const emitted = join(output,records[0].source); const text = readFileSync(emitted,'utf8');
    const declaration = /^let (\$\w+) = 42;$/m.exec(text); assert.ok(declaration);
    assert.ok(text.includes(`.method(globalThis.before(), ${declaration[1]}, globalThis.after())`));
    for (const mode of ['none','getter','before','after','call','rewrite','receiver']) {
      const expected=run(original,mode), actual=run(emitted,mode);
      assert.equal(expected.status,0,expected.stderr); assert.equal(actual.status,0,actual.stderr);
      assert.equal(actual.stdout,expected.stdout,label+'/'+mode);
    }
    const mutations = [
      text.replace('globalThis.receiver.method(', '(0, globalThis.receiver.method)('),
      text.replace('globalThis.before()', '(globalThis.receiver.method, globalThis.before())'),
      text.replace(`, ${declaration[1]},`, ', void 0,'),
      text.replace('globalThis.before()', 'globalThis.after()'),
      `let ${declaration[1]};\n`+text.replace(declaration[0],`${declaration[1]} = 42;`),
    ];
    const expected = run(original,'none');
    for (const [i, mutant] of mutations.entries()) {
      assert.notEqual(mutant,text);
      const file=join(dir,`${label}-mutant-${i}.mjs`);writeFileSync(file,mutant);
      const actual=run(file,'none');assert.ok(actual.status!==0||actual.stdout!==expected.stdout,'undetected mutation '+i);
    }
  }
} finally {rmSync(dir,{recursive:true,force:true});}
