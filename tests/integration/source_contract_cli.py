"""One source target: typed option rejection precedes all filesystem effects."""
import json
from itertools import permutations
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

STRUCTURE, SEMANTIC, FIXTURE = map(Path, sys.argv[1:4])
sys.argv[1:] = []
CONTRACT = 'language_source_v10'
REMOVED = ('--language-arithmetic', '--language-numeric', '--language-constructors')
# Existing CLI wire enums: ExportError::kArguments is appended after kRead.
EXPORT_ARGUMENTS, INSPECT_ARGUMENTS, REMOVED_OPTION = 13, 0, 0


def run(tool, args):
    return subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)


class SourceContractCli(unittest.TestCase):
    def test_removed_options_before_access(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)/'must-not-exist'
            for option in REMOVED:
                for tail in ([option], ['--value-source', option],
                             [option, '--value-source'], [option, option], [option, '--language-source'],
                             ['--language-source', option],
                             *(list(p) for n in (2, 3) for p in permutations(REMOVED, n))):
                    for tool, prefix, code in ((STRUCTURE, ['missing.abc', output], EXPORT_ARGUMENTS),
                                               (SEMANTIC, ['missing.abc', '300'], INSPECT_ARGUMENTS)):
                        with self.subTest(tool=tool.name, options=tail):
                            result = run(tool, [*prefix, *tail])
                            self.assertNotEqual(result.returncode, 0)
                            self.assertEqual(result.stdout, '')
                            rows = result.stderr.splitlines()
                            if tool == STRUCTURE:
                                self.assertTrue(result.stderr.startswith('usage:'))
                            diagnostics = [json.loads(s) for s in rows if s.startswith('{')]
                            self.assertEqual(diagnostics[-1]['error'], code)
                            failure = next(d for d in diagnostics if d.get('domain') == 'abcd::tools::SourceOptionError')
                            self.assertEqual(failure['value'], REMOVED_OPTION)
                            self.assertIn(failure['option'], REMOVED)
                            self.assertIn('removed option', failure['message'])
                            self.assertFalse(output.exists())
                            self.assertEqual(list(Path(temp).iterdir()), [])
                for name in ('abc_scan', 'abc_capability', 'abc_index', 'abc_modules'):
                    result = run(STRUCTURE.with_name(name), [option, 'missing', 'unused'])
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(result.stdout, '')
                    failure = json.loads(result.stderr)
                    self.assertEqual(failure['domain'], 'abcd::tools::SourceOptionError')
                    self.assertEqual(failure['value'], REMOVED_OPTION)
                    self.assertEqual(failure['option'], option)

    def test_invalid_selectors(self):
        for tool, prefix in ((STRUCTURE, ['missing.abc', 'unused-output']),
                             (SEMANTIC, ['missing.abc', '300'])):
            options = [['--source'], ['--shared-source'], ['--value-source', '--value-source'],
                       ['--value-source', '--language-source', '--language-source']]
            if tool == SEMANTIC:
                options += [['--function-source', '--record-source'],
                            ['--instructions', '--record-source'],
                            ['--conditional-values', '--conditional-family'],
                            ['--conditional-family', '--value-source'],
                            ['--family-source-draft', '--record-source']]
            for tail in options:
                result = run(tool, [*prefix, *tail])
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')
                if tool == STRUCTURE:
                    self.assertTrue(result.stderr.startswith('usage:'))
                error = json.loads(result.stderr.splitlines()[-1])
                self.assertEqual(error['error'], EXPORT_ARGUMENTS if tool == STRUCTURE else INSPECT_ARGUMENTS)

    def test_default_alias_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            roots = [Path(temp)/'default', Path(temp)/'alias']
            manifests = []
            for output, options in zip(roots, ([], ['--language-source'])):
                result = run(STRUCTURE, [FIXTURE, output, '--value-source', *options])
                self.assertEqual(result.returncode, 0, result.stderr)
                manifest = json.loads((output/'manifest.json').read_text())
                self.assertEqual(manifest['source_contract'], CONTRACT)
                manifests.append(manifest)
            self.assertEqual(manifests[0], manifests[1])
            complete = [r for r in manifests[0]['records'] if r.get('source_status') == 0]
            self.assertTrue(complete)
            for record in complete:
                self.assertEqual((roots[0]/record['source']).read_bytes(),
                                 (roots[1]/record['source']).read_bytes())
            metadata = json.loads((roots[0]/complete[0]['metadata']).read_text())
            method = metadata['methods'][0]['entity']
            reports = []
            for options in ([], ['--language-source']):
                result = run(SEMANTIC, [FIXTURE, method, '--record-source', *options])
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report['source_contract'], CONTRACT)
                self.assertTrue(report['source_complete'])
                reports.append(report)
            # Process resource measurements are observations, not contract identity.
            for report in reports:
                report.pop('peak_rss_bytes', None)
            self.assertEqual(reports[0], reports[1])


if __name__ == '__main__':
    unittest.main()
