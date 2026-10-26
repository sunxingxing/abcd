// Member-call references across an argument's selection region
// (docs/design/source-recovery/call-references.md). The callee read stays one
// native `o.f(...)` whose reference is evaluated before the arguments; each
// admitted fixture is executed against its original source under a logging
// harness (getters, receiver, rewrite, throw, non-callable). Mutations that
// detach the receiver or re-read the getter must change the trace.
//
// usage: node call_references.mjs ABC_STRUCTURE FIXTURE_DIR
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {copyFileSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const [structure, fixtures] = process.argv.slice(2);
// The ph* fixtures put the call inside try/catch, so they take the phased
// (exception-aware) route; their callee and argument chains start at globalThis.
const complete = ['single', 'twoargs', 'computed', 'rewrite', 'throwing', 'noncallable',
  'phplain', 'phtrue', 'phfalse', 'phthrow', 'phrewrite', 'phnoncall', 'realmlit',
  // Composed across committed selections (call-references.md §5.4).
  'phconcat', 'phtwo', 'phsiblings', 'phprim', 'phprimthrow', 'phrewritearm', 'phassign', 'phthrowarm', 'phorphan'];
const throwsBeforeCall = new Set(['throwing', 'phthrow', 'phprimthrow', 'phthrowarm']);
const directory = mkdtempSync(join(tmpdir(), 'abcd-call-references-'));
try {
  const exportRecord = name => {
    const output = join(directory, 'out-' + name);
    const run = spawnSync(structure, [join(fixtures, name + '.abc'), output, '--value-source',
      '--language-source'], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    const manifest = JSON.parse(readFileSync(join(output, 'manifest.json'), 'utf8'));
    const records = manifest.records.filter(record => record.source_status !== 2);
    assert.equal(records.length, 1, name);
    return {output, record: records[0]};
  };
  const evaluate = file => {
    const run = spawnSync(process.execPath, [join(fixtures, 'harness.mjs'), file], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    return run.stdout.trim();
  };
  const mutations = {detach: 0, reread: 0};
  for (const name of complete) {
    const {output, record} = exportRecord(name);
    assert.equal(record.source_status, 0, name + ' ' + JSON.stringify(record));
    const original = join(directory, name + '.orig.mjs');
    copyFileSync(join(fixtures, name + '.js'), original);
    const decompiled = join(output, record.source);
    const expected = evaluate(original);
    assert.equal(evaluate(decompiled), expected, name);
    const text = readFileSync(decompiled, 'utf8');
    for (const [kind, replace] of [
        ['detach', member => `(0, ${member})(`],
        ['reread', member => `(${member}, ${member.replace(/\.f$/, '')}).f(`]]) {
      // The receiver is unobservable when the argument throws before the call.
      if (!text.includes('globalThis.obj.f(') || (kind === 'detach' && throwsBeforeCall.has(name))) continue;
      const mutated = join(directory, `${name}.${kind}.mjs`);
      writeFileSync(mutated, text.replace('globalThis.obj.f(', replace('globalThis.obj.f')));
      assert.notEqual(evaluate(mutated), expected, `${name} ${kind} mutation must be detected`);
      ++mutations[kind];
    }
  }
  assert.ok(mutations.detach > 0 && mutations.reread > 0, JSON.stringify(mutations));
  // A nested `??` selection without a single consumer stays refused, with the
  // full typed cause chain.
  // Still refused with the same typed chain: a selection nested inside another
  // selection's arm of a concatenation, and a comma side effect inside the
  // argument (a root event outside the call tree breaks the order proof).
  // phorphan now completes: original throwing events are retained through its
  // pure typeof wrapper. It is checked by the differential/mutations above.
  for (const name of ['optarg', 'phnested', 'phcomma']) {
    const {record} = exportRecord(name);
    assert.equal(record.source_status, 3, name);
    const causes = record.source_failure?.causes ?? record.source_causes;
    assert.deepEqual(causes.slice(0,3).map(cause => cause.domain.split('::').at(-1) + '=' + cause.value),
        ['FunctionTreeError=1', 'ExpressionError=4', 'CallPlanError=4'], name);
    assert.deepEqual(causes.slice(3).map(cause=>cause.domain.split('::').at(-1)),
      ['CallReferenceError','EvaluationKind','TemporaryReason'],name);
    const evidence=record.source_failure?.call_reference ?? record.source_call_reference;
    assert.equal(evidence.kind,1,name);
    assert.equal(causes[3].value,evidence.kind);
    assert.equal(causes[4].value,evidence.placement);
    assert.equal(causes[5].value,evidence.temporary);
    assert.ok(Number.isInteger(evidence.call) && Number.isInteger(evidence.property));
  }
} finally {
  rmSync(directory, {recursive: true, force: true});
}
