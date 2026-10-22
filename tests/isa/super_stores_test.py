#!/usr/bin/env python3
"""Strict native super stores with original receiver and inert RHS timing."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'tests/isa'),str(ROOT/'tests/support/isa')]
from native_frame_test import catalog
from oracle_evidence import sha,record_node
from templates_test import compile_source,probe,recover,COMPILER,DISASM,BUILD
FIX=ROOT/'tests/fixtures/isa/super_stores'
def generate():
 entries=[]
 for source in sorted(FIX.glob('*.js')):
  for mode,opts in [('default',[]),('o0',['--opt-level=0'])]:
   abc=source.with_name(source.stem+'-'+mode+'.abc');cmd=compile_source(source,abc,opts)
   entries.append(dict(path=abc.name,sha256=sha(abc),source=source.name,source_sha256=sha(source),command=cmd,complete=source.stem in ['stores','wide'],opcodes=sorted({r['encoding']['opcode'] for r in probe(abc)})))
 (FIX/'manifest.json').write_text(json.dumps(dict(compiler_sha256=sha(COMPILER),disassembler_sha256=sha(DISASM),fixtures=entries),indent=2)+'\n')
 p=ROOT/'tests/isa/coverage_manifest.json';d=json.loads(p.read_text());prefix='tests/fixtures/isa/super_stores/';d['fixtures']=[e for e in d['fixtures'] if not e['path'].startswith(prefix)]
 for e in entries:d['fixtures'].append(dict(path=prefix+e['path'],sha256=e['sha256'],opcodes=e['opcodes'],classification='Unmeasured',oracles={},note='Super receiver/home and primitive-key/inert-RHS proofs. No new premise.'))
 d['oracle_producers']=[e for e in d['oracle_producers'] if e['test']!='isa_super_stores_source_test'];d['oracle_producers'].append(dict(test='isa_super_stores_source_test',producer='tests/isa/super_stores_test.py'));p.write_text(json.dumps(d,indent=2)+'\n')
class SuperStores(unittest.TestCase):
 def test_oracles_and_refusals(self):
  registry=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),registry['compiler_sha256']);self.assertEqual(sha(DISASM),registry['disassembler_sha256'])
  seen=set()
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
     cmd=[('--output='+str(root/'check.abc')) if a.startswith('--output=') else a for a in e['command']];subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True);self.assertEqual(sha(root/'check.abc'),e['sha256'])
     subprocess.run([DISASM,abc,root/'decoded.pa'],check=True,capture_output=True)
     rows=probe(abc);self.assertTrue(all(r['lift']['status']=='ok' for r in rows));out=root/str(i);r=recover(abc,out)
     if not e['complete']:
      self.assertNotEqual(r['source_status'],0,r)
      expected={'effectful':('abcd::source::TreeSemanticError',15),'key':('abcd::source::TreeSemanticError',11),'object':('abcd::source::ObjectBufferError',0)}[source.stem]
      self.assertIn(expected,[(c['domain'],c['value']) for c in r['source_failure']['causes']]);continue
     self.assertEqual(r['source_status'],0,r);seen|={r['encoding']['opcode'] for r in rows};emitted=out/r['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_super_stores.mjs',source,emitted],text=True));record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure')
  for kind in ['STSUPERBYNAME_IMM8_ID16_V8','STSUPERBYNAME_IMM16_ID16_V8','STSUPERBYVALUE_IMM8_V8_V8','STSUPERBYVALUE_IMM16_V8_V8']:self.assertIn(catalog()[kind]['opcode'],seen,kind)
if __name__=='__main__':
 if '--generate' in sys.argv:generate()
 else:unittest.main()
