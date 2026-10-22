#!/usr/bin/env python3
"""Private literal prototype mutations, distinct from spread/data definitions."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'tests/support/isa'),str(ROOT/'tests/support/abc'),str(ROOT/'tests/isa')]
from oracle_evidence import sha,record_node
from builder import Module,FunctionCode,Profile,build
from native_frame_test import catalog,encode
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/object_proto'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER=SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';DISASM=SDK/'toolchains/ark_disasm'
class ObjectProto(unittest.TestCase):
 def test_complete_oracles(self):
  registry=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),registry['compiler_sha256']);self.assertEqual(sha(DISASM),registry['disassembler_sha256'])
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];self.assertEqual(sha(abc),e['sha256'])
     if e['origin']=='constructed':
      c=catalog();op=lambda n,a=():encode(c[n],a);child=FunctionCode('f',(op('LDA_V8',(4,)),op(e['kind'],(0,5)),op('RETURN')),registers=1,arguments=5)
      module=Module((op('DEFINEFUNC_IMM8_ID16_IMM8',(0,1,2)),op('STMODULEVAR_IMM8',(0,)),op('RETURNUNDEFINED')),functions=(child,),exports=('f',),profile=Profile.DYNAMIC2400);self.assertEqual(abc.read_bytes(),build(module)[0])
     else:
      source=FIX/e['source'];self.assertEqual(sha(source),e['source_sha256'])
      command=[('--output='+str(root/'compiled.abc')) if a.startswith('--output=') else a for a in e['command']];subprocess.run(command,check=True,cwd=ROOT,capture_output=True);self.assertEqual(sha(root/'compiled.abc'),e['sha256'])
     subprocess.run([DISASM,abc,root/'decoded.pa'],check=True,capture_output=True)
     rows=[r for line in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(line))['type']=='instruction'];hits=[r for r in rows if r['encoding']['opcode'] in e['opcodes']];self.assertTrue(hits);self.assertTrue(all(r['lift']['status']=='ok' for r in hits))
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);
     if e.get('refused'):
      self.assertNotEqual(record['source_status'],0,record)
      causes=record['source_failure']['causes']
      if e['origin']=='constructed':self.assertIn({'domain':'abcd::source::LiteralError','value':3},causes)
      else:self.assertEqual(causes[-1],{'domain':'abcd::source::EvaluationShapeError','value':0})
      continue
     self.assertEqual(record['source_status'],0,record)
     emitted=out/record['source'];self.assertIn('__proto__:',emitted.read_text());subprocess.run(['node','--check',emitted],check=True,capture_output=True);result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_object_proto.mjs',source,emitted],text=True));record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure')
 def test_scale_records(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for count in [200,201,400,800]:
    source=root/f'scale{count}.js';source.write_text('export function f(p){return ['+','.join('{__proto__:p}' for _ in range(count))+'];}\n')
    abc=root/f'{count}.abc';subprocess.run([COMPILER,'--module','--merge-abc','--extension=js','--record-name=isaProtoScale','--output='+str(abc),source],check=True,capture_output=True)
    out=root/str(count);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2);self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    subprocess.run(['node','--input-type=module','-e',"import assert from 'node:assert/strict'; const a=await import(process.argv[1]),b=await import(process.argv[2]); const p={x:1},x=a.f(p),y=b.f(p);assert.equal(x.length,y.length);assert.equal(new Set(y).size,y.length);for(const v of y)assert.equal(Object.getPrototypeOf(v),p);",source.as_uri(),emitted.as_uri()],check=True,capture_output=True)
if __name__=='__main__':unittest.main()
