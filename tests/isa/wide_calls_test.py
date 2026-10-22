#!/usr/bin/env python3
"""Wide range source calls: SDK provenance, receiver/order and exact raw ranges."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'tests/isa'),str(ROOT/'tests/support/isa')]
from native_frame_test import catalog
from oracle_evidence import sha,record_node
from templates_test import compile_source,probe,recover,COMPILER,DISASM,BUILD
FIX=ROOT/'tests/fixtures/isa/wide_calls'
def generate():
 entries=[]
 for source in sorted([*FIX.glob('calls*.js'),*FIX.glob('arrow*.js')]):
  for mode,opts in [('default',[]),('o0',['--opt-level=0'])]:
   abc=source.with_name(source.stem+'-'+mode+'.abc');cmd=compile_source(source,abc,[*opts,*(['--enable-callable-name'] if '-named' in source.stem else [])])
   entries.append(dict(path=abc.name,sha256=sha(abc),source=source.name,source_sha256=sha(source),command=cmd,complete=source.stem.startswith('calls'),opcodes=sorted({r['encoding']['opcode'] for r in probe(abc)})))
 (FIX/'manifest.json').write_text(json.dumps(dict(compiler_sha256=sha(COMPILER),disassembler_sha256=sha(DISASM),fixtures=entries),indent=2)+'\n')
 p=ROOT/'tests/isa/coverage_manifest.json';d=json.loads(p.read_text());prefix='tests/fixtures/isa/wide_calls/';d['fixtures']=[e for e in d['fixtures'] if not e['path'].startswith(prefix)]
 for e in entries:d['fixtures'].append(dict(path=prefix+e['path'],sha256=e['sha256'],opcodes=e['opcodes'],classification='Unmeasured',oracles={},note='SDK wide calls: occurrence evidence only. WIDE_CALLRANGE requires V8.'))
 d['oracle_producers']=[e for e in d['oracle_producers'] if e['test']!='isa_wide_calls_source_test'];d['oracle_producers'].append(dict(test='isa_wide_calls_source_test',producer='tests/isa/wide_calls_test.py'));p.write_text(json.dumps(d,indent=2)+'\n')
class WideCalls(unittest.TestCase):
 def test_complete_oracles(self):
  data=json.loads((FIX/'manifest.json').read_text());self.assertEqual(sha(COMPILER),data['compiler_sha256']);self.assertEqual(sha(DISASM),data['disassembler_sha256'])
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(data['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
     cmd=[('--output='+str(root/'check.abc')) if a.startswith('--output=') else a for a in e['command']];subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True);self.assertEqual(sha(root/'check.abc'),e['sha256'])
     subprocess.run([DISASM,abc,root/'decoded.pa'],check=True,capture_output=True)
     rows=probe(abc);self.assertTrue(all(r['lift']['status']=='ok' for r in rows))
     if not e.get('complete',True):
      r=recover(abc,root/str(i));self.assertNotEqual(r['source_status'],0)
      expected=('abcd::source::DerivedProjectionError',1) if e['path'].endswith('-o0.abc') else ('abcd::analysis::CatchRegionError',1)
      self.assertIn(expected,[(c['domain'],c['value']) for c in r['source_failure']['causes']]);continue
     n=int(source.stem.split("-")[0][5:]);ops={r['encoding']['opcode'] for r in rows}
     if n>=256 and any(k in source.stem for k in ['direct','template']):self.assertIn(catalog()['WIDE_CALLRANGE_PREF_IMM16_V8']['opcode'],ops)
     out=root/str(i);r=recover(abc,out);self.assertEqual(r['source_status'],0,r);emitted=out/r['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_wide_calls.mjs',source,emitted],text=True));record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure',exclusions=('V7: raw/cooked index descriptors and per-site identity',))
 def test_previous_contract_refuses(self):
  with tempfile.TemporaryDirectory() as d:
   out=Path(d)/'off';subprocess.run([BUILD/'abc_structure',FIX/'calls300-direct-default.abc',out,'--value-source','--language-numeric','--language-constructors'],check=True,capture_output=True)
   self.assertTrue(any(r.get('source_status') not in (0,2) for r in json.loads((out/'manifest.json').read_text())['records']))
if __name__=='__main__':
 if '--generate' in sys.argv:generate()
 else:unittest.main()
