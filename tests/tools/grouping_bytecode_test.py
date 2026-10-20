"""Narrow opt-in allowance; no register, branch or handler normalization."""
import unittest
import json, pathlib, subprocess, tempfile
import sys
sys.dont_write_bytecode = True
from grouping_bytecode import wrapper_load_difference, ES2ABC

class WrapperLoads(unittest.TestCase):
    def test_overwritten_zero(self):
        for load in ['lda v3','ldlexvar 0x0, 0x1','ldexternalmodulevar 0x1',
                     'ldlocalmodulevar 0x0','tryldglobalbyname 0x3, "f"']:
            new='\t'+load+'\n\tsta v4\n\tcallarg0 0x1\n'
            self.assertEqual(len(wrapper_load_difference('\tldai 0x0\n'+new,new,1)),1)
    def test_not_allowed(self):
        new='\tlda v3\n\tcallarg0 0x1\n'
        for old in ['\tldai 0x1\n'+new,'\tldundefined\n'+new,
                    '\tldai 0x0\nlabel:\n'+new,
                    '\tldai 0x0\n\tsta v4\n'+new,
                    '\tldai 0x0\n'+new.replace('v3','v4'),
                    '\tldai 0x0\n'+new.replace('callarg0','callthis0')]:
            self.assertIsNone(wrapper_load_difference(old,new,1))
        self.assertIsNone(wrapper_load_difference('\tldai 0x0\n'+new,new,0))
        self.assertIsNone(wrapper_load_difference(new,'\tldai 0x0\n'+new,1))
class NegatedEqualityWitness(unittest.TestCase):
    @unittest.skipUnless(pathlib.Path(ES2ABC).exists(), 'pinned SDK unavailable')
    def test_exact_compiled_witness(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp)
            for side,text in [('old','export function f(a,b) { return !(a != b); }'),
                              ('new','export function f(a,b) { return a == b; }')]:
                d=root/side;d.mkdir();(d/'input.mjs').write_text(text)
                (d/'manifest.json').write_text(json.dumps({'records':[{'entity':1,'source_status':0,'source':'input.mjs'}]}))
            script=pathlib.Path(__file__).with_name('grouping_bytecode.py')
            def check(name,*flags):
                proc=subprocess.run([sys.executable,str(script),'--old',str(root/'old'),'--new',str(root/'new'),'--out',str(root/name),*flags],capture_output=True,text=True)
                return proc,json.loads((root/name/'report.json').read_text())
            exact,report=check('allowed','--negated-equality')
            self.assertEqual(exact.returncode,0,exact.stdout+exact.stderr)
            self.assertEqual(report['tokens']['pass'],1)
            self.assertGreater(sum(m['allowed'] for m in report['bytecode'].values()),0)
            strict,report=check('strict')
            self.assertNotEqual(strict.returncode,0)
            # An operand replacement cannot be justified by the operator rule.
            (root/'new/input.mjs').write_text('export function f(a,b) { return a == a; }')
            wrong,report=check('wrong','--negated-equality')
            self.assertNotEqual(wrong.returncode,0)
            self.assertEqual(report['tokens']['fail'],1)
if __name__=='__main__':unittest.main()
