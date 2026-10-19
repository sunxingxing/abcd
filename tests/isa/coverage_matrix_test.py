import copy
import pathlib
import sys
import json
import subprocess
import tempfile
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from coverage_matrix import catalog, fingerprint, validate_encoding, validate_registry

class CoverageTest(unittest.TestCase):
    def test_catalog_and_encoding_mutations(self):
        rows=catalog(sys.argv[1],sys.argv[2])
        self.assertEqual(332,len(rows))
        self.assertEqual(332,len({r['opcode'] for r in rows}))
        for spec in rows:
            validate_encoding({'encoding':spec},spec)
        for key in ('prefix','size','opcode_byte'):
            bad=copy.deepcopy(rows[0]);bad[key]=999
            with self.assertRaises(ValueError): validate_encoding({'encoding':bad},rows[0])
        spec=next(r for r in rows if r['format'])
        bad=copy.deepcopy(spec);bad['format'][0]['width']+=1
        with self.assertRaises(ValueError):validate_encoding({'encoding':bad},spec)
    def test_real_probe_continues_after_refusal(self):
        fixture = pathlib.Path(sys.argv[3])
        process = subprocess.run([sys.argv[1], str(fixture)], text=True, capture_output=True, check=True)
        rows = [json.loads(line) for line in process.stdout.splitlines()]
        instructions = [row for row in rows if row['type'] == 'instruction']
        self.assertTrue(instructions)
        digest = fingerprint(fixture)
        specs = {row['opcode']:row for row in catalog(sys.argv[1], sys.argv[2])}
        failed_methods = set()
        continued = False
        for row in instructions:
            self.assertEqual(digest, row['sha256'])
            validate_encoding(row, specs[row['encoding']['opcode']])
            if row['lift']['status'] == 'refused':
                failed_methods.add(row['method'])
            elif row['lift']['status'] == 'ok':
                continued |= row['method'] in failed_methods
                self.assertIn('roles', row['lift'])
                self.assertIn('normal_commit', row['lift'])
                self.assertIn('exception_exit', row['lift'])
        self.assertTrue(continued)
        seen = {r['encoding']['opcode'] for r in instructions}
        validate_registry({'sha256':digest,'opcodes':list(seen)},digest,seen)

    def test_manifest_join_and_stale_hash(self):
        fixture = pathlib.Path(sys.argv[3])
        raw = subprocess.run([sys.argv[1], str(fixture)], text=True, capture_output=True, check=True)
        site = next(json.loads(line) for line in raw.stdout.splitlines()
                    if json.loads(line)['type'] == 'instruction')
        failure = {'method':site['method'], 'pc':0, 'causes':[{'domain':'test::Failure','value':1}]}
        manifest = {'sha256':fingerprint(fixture), 'records':[{'entity':site['record'],
                    'source_status':3, 'source_failure':failure}]}
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'manifest.json'
            path.write_text(json.dumps(manifest))
            joined = subprocess.run([sys.argv[1], str(fixture), str(path), str(path)],
                                    text=True, capture_output=True, check=True)
            row = next(json.loads(line) for line in joined.stdout.splitlines()
                       if json.loads(line)['type'] == 'instruction')
            self.assertEqual(failure, row['recovery']['v4']['method_first_refusal'])
            self.assertEqual(0, row['recovery']['previous']['record_first_refusal']['pc'])
            manifest['sha256'] = 'stale'; path.write_text(json.dumps(manifest))
            refused = subprocess.run([sys.argv[1], str(fixture), str(path), str(path)],
                                     text=True, capture_output=True)
            self.assertNotEqual(0, refused.returncode)
            self.assertEqual('', refused.stdout)
            self.assertEqual('input_failure', json.loads(refused.stderr)['type'])

    def test_registry_mutations(self):
        e={'sha256':'abc','opcodes':[1]}
        validate_registry(e,'abc',{1})
        with self.assertRaises(ValueError):validate_registry(e,'stale',{1})
        with self.assertRaises(ValueError):validate_registry(e,'abc',{2})
        e['classification']='SupportedVerified';e['lift_status']='refused'
        with self.assertRaises(ValueError):validate_registry(e,'abc',{1})
        e.pop('classification');e['oracles']={'node':{'status':'pass','skipped':True,'evidence':'log'}}
        with self.assertRaises(ValueError):validate_registry(e,'abc',{1})

if __name__=='__main__':
    unittest.main(argv=[sys.argv[0]],exit=True)
