// Record failure isolation (S3): a malformed scope-name string of one record
// is a format error of that record, even though the recovery stage folds it
// into RecordProgramError::kRead. Without an out-of-band process fault
// (allocation, input read, input change) the export succeeds, the record
// carries its typed causes, and every other record keeps its outcome.
//
// usage: node record_isolation.mjs ABC_STRUCTURE SAMPLE_ABC
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const [structure, sample] = process.argv.slice(2);
// A module record of the fixed regression sample whose source recovery reads
// its scope names (the index page of the paired Immersive application).
const kDescriptor = 'L&default/src/main/ets/pages/Index&;';
const kChecksumOffset = 8, kChecksumStart = 12, kStringTag = 5;

const adler32 = bytes => {
  let a = 1, b = 0;
  for (const byte of bytes) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  return ((b << 16) | a) >>> 0;
};
const uleb = (bytes, offset) => {
  let shift = 0;
  while (bytes[offset++] & 0x80) shift += 7;
  return offset;
};
const directory = mkdtempSync(join(tmpdir(), 'abcd-record-isolation-'));
try {
  const exportTo = (input, output) => spawnSync(structure, [input, output, '--value-source'], {encoding: 'utf8'});
  const baseline = join(directory, 'baseline');
  assert.equal(exportTo(sample, baseline).status, 0);
  const manifest = path => JSON.parse(readFileSync(join(path, 'manifest.json'), 'utf8'));
  const before = manifest(baseline);
  const target = before.records.find(record =>
    JSON.parse(readFileSync(join(baseline, record.metadata), 'utf8')).descriptor === kDescriptor);
  assert.ok(target, 'target record present');
  assert.equal(target.source_status, 0, 'target recovers before corruption');
  const metadata = JSON.parse(readFileSync(join(baseline, target.metadata), 'utf8'));
  const literal = metadata.record_fields.find(field => field.name === 'scopeNames').value.bits;

  const bytes = new Uint8Array(readFileSync(sample));
  const view = new DataView(bytes.buffer);
  assert.ok(view.getUint32(literal, true) > 0);
  assert.equal(bytes[literal + 4], kStringTag);
  const string = view.getUint32(literal + 5, true);
  // 0xff is never a valid MUTF-8 lead byte.
  bytes[uleb(bytes, string)] = 0xff;
  view.setUint32(kChecksumOffset, adler32(bytes.subarray(kChecksumStart)), true);
  const corrupted = join(directory, 'corrupted.abc');
  writeFileSync(corrupted, bytes);

  const output = join(directory, 'corrupted');
  const run = exportTo(corrupted, output);
  assert.equal(run.status, 0, 'a record format error must not abort the export: ' + run.stderr);
  const after = manifest(output);
  assert.equal(after.records.length, before.records.length);
  let complete = 0;
  for (const [i, record] of after.records.entries()) {
    assert.equal(record.entity, before.records[i].entity);
    if (record.entity !== target.entity) {
      assert.equal(record.source_status, before.records[i].source_status, 'record ' + record.entity);
      complete += record.source_status === 0;
      continue;
    }
    assert.equal(record.source_status, 3);
    assert.equal(record.source_failure_domain, 'record_program');
    assert.ok(record.source_causes.some(cause => cause.domain === 'abcd::abc::MetadataError'),
      JSON.stringify(record.source_causes));
    assert.equal(record.source, undefined);
  }
  assert.ok(complete > 0);
} finally {
  rmSync(directory, {recursive: true, force: true});
}
