#!/usr/bin/env python3
"""V10 namespace imports: GETMODULENAMESPACE (imm8, WIDE) -> `import * as ns`."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import sha,record_node
BUILD=ROOT/'build/regression';FIX=ROOT/'tests/fixtures/isa/module_namespace'
HARNESS=ROOT/'tests/integration/module_namespace.mjs'
def trace(path,kind):
  return json.loads(subprocess.check_output(['node',HARNESS,path,kind],text=True))
class Namespaces(unittest.TestCase):
 def test_sources(self):
  manifest=json.loads((FIX/'manifest.json').read_text())
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for i,e in enumerate(manifest['fixtures']):
    abc=ROOT/e['path'];source=ROOT/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
    out=root/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    text=emitted.read_text();self.assertIn('import * as ',text);self.assertNotIn('getmodulenamespace',text)
    kind=e['kind'];expected=trace(source,kind);observed=trace(emitted,kind)
    self.assertEqual(observed,expected,abc)
    record_node(abc,out,source,__file__,len(expected),BUILD/'abc_structure',
                exclusions=['ark-native-cjs-namespace'],dependencies=[HARNESS])
 def test_scale(self):
  compiler=json.loads((FIX/'manifest.json').read_text())['compiler']
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)
   for n in (200,201,400,800):
    source=root/(str(n)+'.js');abc=source.with_suffix('.abc')
    lines=[f'import * as n{i} from "data:text/javascript,export const v = {i};";' for i in range(n)]
    # One function per 100 bindings: a single function capturing 800 lexical
    # bindings reaches the pre-existing printer storage limit (also on plain
    # captured lets), which is independent of namespace imports.
    parts=[list(range(n))[k:k+100] for k in range(0,n,100)]
    lines+=[f'function part{j}() {{ return '+' + '.join(f'n{i}.v' for i in part)+'; }' for j,part in enumerate(parts)]
    lines.append('export function sum() { return '+' + '.join(f'part{j}()' for j in range(len(parts)))+'; }')
    source.write_text('\n'.join(lines)+'\n')
    subprocess.run([compiler,'--module','--merge-abc','--extension=js','--record-name=scaleNamespace','--output='+str(abc),source],check=True,capture_output=True)
    out=root/(str(n)+'-out');subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
    record=json.loads((out/'manifest.json').read_text())['records'][-1];self.assertEqual(record['source_status'],0,record)
    emitted=out/record['source'];subprocess.run(['node','--check',emitted],check=True,capture_output=True)
    run=lambda p:subprocess.check_output(['node','--input-type=module','-e',
      "import {readFileSync} from 'node:fs';const m=await import('data:text/javascript;base64,'+Buffer.from(readFileSync(process.argv[1])).toString('base64'));console.log(m.sum());",str(p)],text=True)
    self.assertEqual(run(emitted),run(source));self.assertEqual(int(run(source)),n*(n-1)//2)
if __name__=='__main__':unittest.main()
