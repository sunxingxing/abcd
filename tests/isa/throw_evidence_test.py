#!/usr/bin/env python3
"""Runtime throw meanings and exact current recovery boundaries, not fake support."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/throws'
SDK=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony');DISASM=SDK/'toolchains/ark_disasm'
class Throws(unittest.TestCase):
 def test_emission_and_recovery(self):
  registry=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
     cmd=[('--output='+str(root/'check.abc')) if x.startswith('--output=') else x for x in e['command']];subprocess.run(cmd,check=True,cwd=ROOT,capture_output=True);self.assertEqual(sha(root/'check.abc'),e['sha256']);subprocess.run([DISASM,abc,root/'out.pa'],check=True,capture_output=True)
     text=(root/'out.pa').read_text()
     if source.stem=='delegate':self.assertIn('throw.notexists',text)
     if source.stem=='const':self.assertIn('throw.constassignment',text)
     if source.stem=='delete_super':self.assertNotIn('throw.deletesuperproperty',text);self.assertIn('delobjprop',text)
     out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True);record=next(r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2)
     if source.stem not in ['super','delete_super']:
      self.assertNotEqual(record['source_status'],0,record);self.assertTrue(record.get('source_failure'),record);continue
     # Default builds complete since 2026-10-23 (copy-separated module guards
     # and sta/lda rethrow handlers); they take the same checks as O0.
     self.assertEqual(record['source_status'],0,record);emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     if source.stem=='delete_super':
      # The two observations MUST differ. The SDK lowers delete-super as
      # delete-this; no Node oracle attests the source to that bytecode.
      cmd=['node','--input-type=module','-e',"import assert from 'node:assert/strict';const a=await import(process.argv[1]),b=await import(process.argv[2]);assert.throws(()=>new a.C().method(),ReferenceError);const x=new b.C();x.x=1;assert.equal(x.method(),true);assert.equal(Object.hasOwn(x,'x'),false);",source.as_uri(),emitted.as_uri()];subprocess.run(cmd,check=True,capture_output=True)
     else:
      subprocess.run(['node','--input-type=module','-e',"import assert from 'node:assert/strict';const a=await import(process.argv[1]),b=await import(process.argv[2]);for(const v of [undefined,null,0,1,'x',{}]){const x=new a.C(v),y=new b.C(v);assert.equal(x.value,y.value);assert.equal(x.extra,y.extra);}",source.as_uri(),emitted.as_uri()],check=True,capture_output=True);record_node(abc,out,source,__file__,6,BUILD/'abc_structure')
if __name__=='__main__':unittest.main()
