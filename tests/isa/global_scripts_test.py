#!/usr/bin/env python3
"""V9 script entry, global identity and original checked-write error sources."""
import json,subprocess,sys,tempfile,unittest,os
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/global_scripts'
class Scripts(unittest.TestCase):
 def test_sources(self):
  registry=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   scratch=Path(d)
   for i,e in enumerate(registry['fixtures']):
    with self.subTest(fixture=e['path']):
     abc=ROOT/e['path'];original=ROOT/e['source'];kind=original.stem
     self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(original),e['source_sha256']);self.assertNotIn('--module',e['command'])
     export=scratch/str(i);subprocess.run([BUILD/'abc_structure',abc,export,'--value-source','--language-source'],check=True,capture_output=True)
     manifest=json.loads((export/'manifest.json').read_text());records=[r for r in manifest['records'] if r.get('source_language')=='javascript-script']
     self.assertEqual(len(records),1);record=records[0]
     self.assertEqual(record['source_status'],0,record);self.assertEqual(manifest['source_contract'],'language_source_v9')
     emitted=export/record['source'];self.assertEqual(emitted.suffix,'.js');subprocess.run(['node','--check',emitted],check=True,capture_output=True)
     observed=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/global_scripts.mjs',original,emitted,kind],text=True))
     previous=scratch/(str(i)+'-previous');subprocess.run([BUILD/'abc_structure',abc,previous,'--value-source','--language-numeric','--language-constructors'],check=True,capture_output=True)
     self.assertTrue(all(r['source_status']==2 for r in json.loads((previous/'manifest.json').read_text())['records']))
     if kind!='sloppy':record_node(abc,export,original,__file__,observed['cases'],BUILD/'abc_structure',exclusions=['ark-late-global-lexical-activation','ark-sloppy-unresolved-write','ark-entry-global-accessor-set'],dependencies=[ROOT/'tests/integration/global_scripts.mjs'])
 def test_host_accessor_boundary(self):
  # Node establishes the standard side only. The pinned Ark SetProperty path
  # is cited in isa-global-scripts.md; this is not an Ark-runtime oracle.
  subprocess.run(['node','--input-type=module','-e',r"""
import assert from 'node:assert/strict';import vm from 'node:vm';
let id=0;
const run=source=>{const trace=[];const name='hostAccessor'+(++id);
Object.defineProperty(globalThis,name,{configurable:true,set(v){trace.push(typeof v);}});
new vm.Script(source.replaceAll('TARGET',name)).runInThisContext();return trace;};
assert.deepEqual(run('var TARGET;'),[]);
assert.deepEqual(run('function TARGET() {}'),[]);
assert.deepEqual(run('"use strict"; TARGET = undefined;'),['undefined']);
assert.deepEqual(run('"use strict"; TARGET = function named() {};'),['function']);
"""],check=True,capture_output=True)
 def test_scale(self):
  compiler=json.loads((FIX/'manifest.json').read_text())['compiler']
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for count in (200,201,400,800):
    source=root/(str(count)+'.js');abc=source.with_suffix('.abc')
    source.write_text('"use strict";\nlet mutable=0;\n'+'mutable=mutable+1;\n'*count+'globalThis.scriptObservation=mutable;\n')
    subprocess.run([compiler,'--merge-abc','--extension=js','--record-name=scaleScripts','--output='+str(abc),str(source)],check=True,capture_output=True)
    export=root/(str(count)+'-out');subprocess.run([BUILD/'abc_structure',abc,export,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((export/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=export/record['source'];observed=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/global_scripts.mjs',source,emitted,'lexical_wide'],text=True));self.assertGreater(observed['cases'],0)
if __name__=='__main__':unittest.main()
