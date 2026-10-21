#!/usr/bin/env python3
"""Complete SDK records for wide/property/pool-literal ISA occurrences."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/support/isa'))
from oracle_evidence import record_node,sha
FIX=ROOT/'tests/fixtures/isa/recovery'
BUILD=ROOT/'build/regression'
COMPILER=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc')
DISASM=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/ark_disasm')

def probe(path):
    p=subprocess.Popen([BUILD/'isa_coverage_probe',path],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    try:
        for line in p.stdout:
            row=json.loads(line)
            if row['type']=='instruction':yield row
        if p.wait():raise ValueError('ISA probe failed')
    finally:
        p.stdout.close()
        if p.poll() is None:p.kill();p.wait()

def generate():
    entries=[]
    for source in sorted(FIX.glob('*.js')):
        for mode,flags in [('default',[]),('o0',['--opt-level=0'])]:
            path=source.with_name(source.stem+'-'+mode+'.abc')
            cmd=[str(COMPILER),'--module','--merge-abc','--extension=js','--record-name=isaRecovery',
                '--source-file='+source.name,*flags,'--output='+str(path.relative_to(ROOT)),str(source.relative_to(ROOT))]
            subprocess.run(cmd,check=True,cwd=ROOT)
            with tempfile.TemporaryDirectory() as d:
                out=Path(d)/'output'
                subprocess.run([BUILD/'abc_structure',path,out,'--value-source','--language-source'],check=True,capture_output=True)
                records=[r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2]
                assert len(records)==1
                recovery={'status':records[0]['source_status'],'causes':records[0].get('source_failure',{}).get('causes',records[0].get('source_causes',[]))}
            entries.append({'recovery':recovery,'path':path.name,'sha256':sha(path),'source':source.name,'source_sha256':sha(source),
                'command':cmd,'opcodes':sorted({r['encoding']['opcode'] for r in probe(path)})})
    (FIX/'manifest.json').write_text(json.dumps({'compiler_sha256':sha(COMPILER),'disassembler_sha256':sha(DISASM),'fixtures':entries},indent=2)+'\n')
    registry=ROOT/'tests/isa/coverage_manifest.json';data=json.loads(registry.read_text());prefix='tests/fixtures/isa/recovery/'
    data['fixtures']=[r for r in data['fixtures'] if not r['path'].startswith(prefix)]
    for e in entries:data['fixtures'].append({'path':prefix+e['path'],'sha256':e['sha256'],'opcodes':e['opcodes'],'classification':'Unmeasured','oracles':{},'note':'Only an executed occurrence oracle can promote this fixture.'})
    producers=data['oracle_producers']
    if not any(r['test']=='isa_recovery_variants_test' for r in producers):producers.append({'test':'isa_recovery_variants_test','producer':'tests/isa/recovery_variants_test.py'})
    registry.write_text(json.dumps(data,indent=2)+'\n')

class Recovery(unittest.TestCase):
    def test_sdk_complete_differentials(self):
        manifest=json.loads((FIX/'manifest.json').read_text())
        self.assertEqual(sha(COMPILER),manifest['compiler_sha256']);self.assertEqual(sha(DISASM),manifest['disassembler_sha256'])
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d)
            for i,e in enumerate(manifest['fixtures']):
                with self.subTest(fixture=e['path']):
                    abc=FIX/e['path'];source=FIX/e['source'];self.assertEqual(sha(abc),e['sha256']);self.assertEqual(sha(source),e['source_sha256'])
                    cmd=[('--output='+str(tmp/'compiled.abc')) if arg.startswith('--output=') else arg for arg in e['command']]
                    subprocess.run(cmd,check=True,cwd=ROOT);self.assertEqual(sha(tmp/'compiled.abc'),e['sha256'])
                    rows=list(probe(abc));self.assertEqual(sorted({r['encoding']['opcode'] for r in rows}),e['opcodes'])
                    self.assertTrue(all(r['lift']['status']=='ok' for r in rows))
                    subprocess.run([DISASM,abc,tmp/'disassembled.pa'],check=True,capture_output=True)
                    out=tmp/str(i);subprocess.run([BUILD/'abc_structure',abc,out,'--value-source','--language-source'],check=True,capture_output=True)
                    records=[r for r in json.loads((out/'manifest.json').read_text())['records'] if r.get('source_status')!=2]
                    self.assertEqual(len(records),1)
                    actual={'status':records[0]['source_status'],'causes':records[0].get('source_failure',{}).get('causes',records[0].get('source_causes',[]))}
                    self.assertEqual(actual,e['recovery'])
                    if actual['status']!=0:
                        # Explicitly measured optimized-body refusal, not an oracle.
                        self.assertTrue(actual['causes']);self.assertTrue(e['path'].endswith('-default.abc'))
                        continue
                    result=subprocess.check_output(['node','--expose-internals','--experimental-vm-modules',ROOT/'tests/integration/isa_recovery.mjs',source,out/records[0]['source'],source.stem],text=True)
                    cases=json.loads(result)['cases'];self.assertGreater(cases,0)
                    record_node(abc,out,source,__file__,cases,BUILD/'abc_structure',
                        exclusions=['lazy imports tested with pre-initialized synthetic dependencies'] if source.stem=='module-lazy' else [])

    def test_corpus_decode(self):
        with tempfile.TemporaryDirectory() as d:
            index=Path(d)/'index.json';subprocess.run([BUILD/'abc_scan',index,'isaRecovery',FIX],check=True,capture_output=True)
            report=json.loads(subprocess.check_output([BUILD/'abc_decode',index,'isaRecovery',FIX],text=True))
            self.assertEqual(report['failed_methods'],0);self.assertEqual(report['failed'],0)

if __name__=='__main__':
    if '--generate' in sys.argv:generate()
    else:unittest.main()
