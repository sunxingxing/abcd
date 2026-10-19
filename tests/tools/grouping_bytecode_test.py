"""Narrow opt-in allowance; no register, branch or handler normalization."""
import unittest
import sys
sys.dont_write_bytecode = True
from grouping_bytecode import wrapper_load_difference

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
if __name__=='__main__':unittest.main()
