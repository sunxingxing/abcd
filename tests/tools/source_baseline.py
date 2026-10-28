#!/usr/bin/env python3
"""Whole-file source-recovery baseline: run abc_structure once, then summarize.

usage: source_baseline.py ABC_STRUCTURE INPUT.abc OUTPUT_DIR SUMMARY.json

The summary records wall time, the CLI's own peak RSS, per-status record
counts, complete/partial source bytes, the first typed cause of each refusal
(and of each omitted member), and a syntax check of every published file.
Paired source never enters the engine; this script only reads its output.
"""
import collections
import json
from pathlib import Path
import subprocess
import sys
import time

STATUS = {0: 'complete', 1: 'module_context', 2: 'no_module', 3: 'refused', 4: 'partial'}


def cause_key(causes):
    if not causes:
        return 'none'
    return ' > '.join(f"{c['domain'].split('::')[-1]}:{c['value']}" for c in causes[:3])


def main():
    tool, abc, out, summary_path = map(Path, sys.argv[1:5])
    started = time.monotonic()
    run = subprocess.run([tool, abc, out, '--value-source'], capture_output=True, text=True)
    elapsed = time.monotonic() - started
    cli = {}
    for line in reversed(run.stdout.splitlines()):
        try:
            cli = json.loads(line)
            break
        except ValueError:
            continue
    manifest = json.loads((out / 'manifest.json').read_text())
    statuses = collections.Counter()
    refusals = collections.Counter()
    omissions = collections.Counter()
    complete_bytes = partial_bytes = 0
    published = []
    for record in manifest['records']:
        status = record.get('source_status')
        statuses[STATUS.get(status, str(status))] += 1
        if status == 0:
            complete_bytes += (out / record['source']).stat().st_size
            published.append(out / record['source'])
        elif status == 4:
            partial_bytes += (out / record['incomplete_source']).stat().st_size
            published.append(out / record['incomplete_source'])
            for member in record.get('source_omitted_members', []):
                omissions[cause_key(member.get('causes'))] += 1
        elif status == 3:
            refusals[cause_key(record.get('source_failure', {}).get('causes'))] += 1
    syntax_errors = []
    for path in published:
        check = subprocess.run(['node', '--check', path], capture_output=True, text=True)
        if check.returncode:
            syntax_errors.append(str(path.relative_to(out)))
    executable = statuses['complete'] + statuses['partial'] + statuses['refused']
    summary = {
        'input': str(abc), 'exit_code': run.returncode, 'wall_seconds': round(elapsed, 1),
        'peak_rss_bytes': cli.get('peak_rss_bytes'), 'records': sum(statuses.values()),
        'statuses': dict(statuses),
        'complete_rate_of_executable': round(statuses['complete'] / executable, 4) if executable else None,
        'complete_source_bytes': complete_bytes, 'partial_source_bytes': partial_bytes,
        'syntax_errors': syntax_errors,
        'refusal_causes': dict(refusals.most_common()),
        'omission_causes': dict(omissions.most_common()),
    }
    summary_path.write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps({k: summary[k] for k in ('wall_seconds', 'peak_rss_bytes', 'records', 'statuses',
                                               'complete_rate_of_executable')}))


if __name__ == '__main__':
    main()
