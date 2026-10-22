"""Importer rejects profile transfer, stale proof artifacts and fake passes."""
import copy
import gzip
import json
from pathlib import Path
import tempfile
import unittest
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).parent))
from oracle_import import OracleIndex, sha
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"support/isa"))
from oracle_evidence import EventBudget, OracleResourceError
ROOT = Path(__file__).resolve().parents[2]


class ImportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        def file(name, text):
            p = self.directory/name
            p.write_text(text)
            return {'path': str(p), 'sha256': sha(p)}
        self.producer = file('producer.py', '# trusted unit fixture')
        self.evidence = {'schema': 1, 'cases': 1,
            'writer': {'path': str(ROOT/'tests/support/isa/oracle_evidence.py'),
                       'sha256': sha(ROOT/'tests/support/isa/oracle_evidence.py')},
            'producer': self.producer, 'engine': file('engine', 'engine'),
            'probe': file('probe', 'probe'), 'abc': file('input.abc', 'abc'),
            'original': file('original.js', 'export{}'),
            'contract': 'language_source_v8', 'records': []}
        self.evidence['manifest'] = file('manifest.json', json.dumps({
            'sha256': self.evidence['abc']['sha256'], 'source_contract': 'language_source_v8',
            'records': [{'entity': 1, 'source_status': 0}]}))
        self.evidence['records'] = [{'record': 1, 'emitted': file('emitted.mjs', 'export{}'),
                                     'round_trip': None}]
        self.row = {'sha256': self.evidence['abc']['sha256'], 'profile': 6,
                    'record': 1, 'method': 2, 'pc': 0, 'opcode': 10,
                    'semantic_family': 'typed::Family', 'node': {'status': 'pass'},
                    'ark_round_trip': None, 'ark_runtime': None}
        self.path = self.directory/'evidence.json'
        self.persist()

    def persist(self):
        p = self.directory/'occurrences.jsonl.gz'
        with gzip.open(p, 'wt') as output:
            output.write(json.dumps(self.row)+'\n')
        self.evidence['occurrences'] = {'path': str(p), 'sha256': sha(p)}
        self.path.write_text(json.dumps(self.evidence))

    def index(self):
        i = OracleIndex(self.directory/'index.sqlite', self.evidence['engine']['sha256'],
                        self.evidence['probe']['sha256'], [self.producer['path']])
        self.addCleanup(i.close)
        return i

    def lookup(self):
        return {**self.row, 'encoding': {'opcode': self.row['opcode']},
                'lift': {'status': 'ok', 'semantic_family': self.row['semantic_family']},
                'recovery': {'v4': {'record_status': 0}}}

    def test_exact_event_budget(self):
        for n in (200, 201, 400, 800):
            charge = 8192
            row = {'method': 1, 'pc': 0, '_oracle_charge_bytes': charge}
            budget = EventBudget(n * charge)
            for _ in range(n): budget.admit(row)
            self.assertEqual(budget.remaining, 0)
            with self.assertRaises(OracleResourceError): budget.admit(row)
            budget = EventBudget(n * charge - 1)
            for _ in range(n-1): budget.admit(row)
            with self.assertRaises(OracleResourceError): budget.admit(row)

    def test_key_and_profile_isolation(self):
        index = self.index()
        index.import_file(self.path)
        self.assertEqual(index.lookup(self.lookup())['node']['status'], 'pass')
        for key, value in [('profile', 5), ('method', 3), ('pc', 1), ('sha256', 'other')]:
            row = self.lookup(); row[key] = value
            self.assertIsNone(index.lookup(row)['node'])
        self.assertIsNone(index.lookup(self.lookup())['ark_runtime'])

    def test_recovery_and_lift_not_oracles(self):
        index = self.index(); index.import_file(self.path)
        bad = self.lookup(); bad['lift']['status'] = 'refused'
        with self.assertRaises(ValueError): index.lookup(bad)
        bad = self.lookup(); bad['recovery']['v4']['record_status'] = 3
        with self.assertRaises(ValueError): index.lookup(bad)
        bad = self.lookup(); bad['encoding']['opcode'] += 1
        with self.assertRaises(ValueError): index.lookup(bad)

    def test_stale_artifact(self):
        Path(self.evidence['original']['path']).write_text('changed')
        with self.assertRaises(ValueError): self.index().import_file(self.path)

    def test_skipped_node(self):
        self.row['node']['skipped'] = True; self.persist()
        with self.assertRaises(ValueError): self.index().import_file(self.path)

    def test_incomplete_record(self):
        self.evidence['records'][0]['record'] = 9; self.persist()
        with self.assertRaises(ValueError): self.index().import_file(self.path)

    def test_valid_roundtrip_is_separate(self):
        witness = {'status': 'pass', 'semantic_family': 'typed::Family', 'method': 3, 'pc': 5,
                   'profile': 6, 'opcode': 11}
        receipt = {'compiler': self.evidence['engine'], 'disassembler': self.evidence['probe'],
                   'abc': self.evidence['abc'], 'mismatches': [],
                   'events': [{'method': 2, 'pc': 0, 'witness': witness}]}
        path = self.directory/'round-trip.json';path.write_text(json.dumps(receipt))
        self.evidence['records'][0]['round_trip'] = {'path': str(path), 'sha256': sha(path)}
        self.row['node'] = None;self.row['ark_round_trip'] = witness;self.persist()
        index=self.index();index.import_file(self.path);actual=index.lookup(self.lookup())
        self.assertIsNone(actual['node']);self.assertEqual(actual['ark_round_trip']['status'],'pass')

    def test_forged_roundtrip(self):
        self.row['ark_round_trip'] = {'status': 'pass', 'semantic_family': 'typed::Family',
                                     'method': 3, 'pc': 5}; self.persist()
        with self.assertRaises(ValueError): self.index().import_file(self.path)

    def test_runtime_unavailable(self):
        self.row['ark_runtime'] = {'status': 'pass'}; self.persist()
        with self.assertRaises(ValueError): self.index().import_file(self.path)

    def test_stale_producer_and_binary(self):
        self.evidence['producer']['sha256'] = 'stale'; self.persist()
        with self.assertRaises(ValueError): self.index().import_file(self.path)


if __name__ == '__main__':
    unittest.main()
