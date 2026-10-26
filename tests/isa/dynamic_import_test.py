#!/usr/bin/env python3
"""V10 import promise: one specifier, one job, caller versus rejection exits."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/dynamic_import'
class Imports(unittest.TestCase):
 def test_sources(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(manifest['fixtures']):
    abc=ROOT/e['path'];source=ROOT/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
    out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    observed=json.loads(subprocess.check_output(['node',ROOT/'tests/integration/dynamic_import.mjs',source,emitted],text=True))
    record_node(abc,out,source,__file__,observed['cases'],BUILD/'abc_structure',exclusions=observed['exclusions'],dependencies=[ROOT/'tests/integration/dynamic_import.mjs'])
 def test_scale(self):
  compiler=json.loads((FIX/'manifest.json').read_text())['compiler']
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for n in (200,201,400,800):
    source=root/(str(n)+'.js');abc=source.with_suffix('.abc');source.write_text('export function run(x){return ['+','.join(['import(x)']*n)+'];}')
    subprocess.run([compiler,'--module','--merge-abc','--extension=js','--record-name=scaleImport','--output='+str(abc),source],check=True,capture_output=True)
    out=root/(str(n)+'-out');subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    subprocess.run(['node','--input-type=module','-e',r'''
import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';
const load=p=>import('data:text/javascript;base64,'+Buffer.from(readFileSync(p)).toString('base64'));
const [original,emitted,n]=process.argv.slice(1);const modules=await Promise.all([load(original),load(emitted)]);
for(const m of modules){const trace=[];const input={toString(){trace.push('coerce');return 'data:text/javascript,export default 17;';}};const promises=m.run(input);assert.equal(promises.length,+n);assert.equal(new Set(promises).size,+n);assert.equal(trace.length,+n);const values=await Promise.all(promises);assert.ok(values.every(v=>v.default===17));}
''',source,emitted,str(n)],check=True,capture_output=True)
if __name__=='__main__':unittest.main()
