#!/usr/bin/env python3
"""V7 private object-spread construction; Node and Ark evidence stay separate."""
import json, subprocess, sys, tempfile, unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tests/support/isa')]
from oracle_evidence import sha, record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/object_spread'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony')
COMPILER=SDK/'ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc';DISASM=SDK/'toolchains/ark_disasm'
class ObjectSpread(unittest.TestCase):
    def test_complete_oracles(self):
        registry=json.loads((FIX/'manifest.json').read_text())
        self.assertEqual(sha(COMPILER),registry['compiler_sha256']);self.assertEqual(sha(DISASM),registry['disassembler_sha256'])
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for i,e in enumerate(registry['fixtures']):
                with self.subTest(fixture=e['path']):
                    abc=FIX/e['path'];source=FIX/e['source']
                    self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
                    if e['origin']=='es2abc':
                        command=[('--output='+str(root/'compiled.abc')) if a.startswith('--output=') else a for a in e['command']]
                        subprocess.run(command,check=True,cwd=ROOT);self.assertEqual(sha(root/'compiled.abc'),e['sha256'])
                    subprocess.run([DISASM,abc,root/'decoded.pa'],check=True,capture_output=True)
                    rows=[r for x in subprocess.check_output([BUILD/'isa_coverage_probe',abc],text=True,stderr=subprocess.DEVNULL).splitlines() if (r:=json.loads(x))['type']=='instruction']
                    hits=[r for r in rows if r['encoding']['opcode'] in e['opcodes']]
                    self.assertTrue(hits);self.assertTrue(all(r['lift']['status']=='ok' for r in hits))
                    out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
                    record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2)
                    self.assertEqual(record['source_status'],0,record)
                    emitted=out/record['source'];self.assertIn('...',emitted.read_text());subprocess.run(['node','--check',emitted],check=True,capture_output=True)
                    result=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/isa_object_spread.mjs',source,emitted],text=True))
                    record_node(abc,out,source,__file__,result['cases'],BUILD/'abc_structure',exclusions=['V7 CopyDataProperties preserves source accessors instead of invoking getters; accessor/Proxy descriptor-vs-value observations are excluded from the Ark equivalence claim. Node compares the two native sources only.'])
    def test_premise_off(self):
        with tempfile.TemporaryDirectory() as d:
            for mode in ['default','o0']:
                out=Path(d)/mode
                subprocess.run([BUILD/'abc_structure',FIX/('spread-'+mode+'.abc'),out,'--value-source','--language-numeric','--language-constructors'],check=True,capture_output=True)
                record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2)
                self.assertNotEqual(record['source_status'],0,record);self.assertTrue(record.get('source_failure') or record.get('source_omitted_members'))
if __name__=='__main__':unittest.main()
