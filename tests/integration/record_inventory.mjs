import assert from 'node:assert/strict';

// Independent test-only census, not a second ABC decoder. Layout provenance:
// pinned runtime_core 3e147bb, libpandafile/file.h Header and
// class_data_accessor.cpp ClassDataAccessor. Read class headers directly so a
// source-export omission cannot justify itself with another exported count.
// O(records + descriptor bytes); never reads code/debug/source sidecars.
const header = {size: 60, version: 12, fileSize: 16, foreign: 20, foreignSize: 24, count: 28, index: 32};
const wordBytes = 4, lebBits = 7, lebPayload = 0x7f, lebContinuation = 0x80, maxLebBytes = 5;
const noModuleStatus = 2;

export function verifyRecordInventory(bytes, manifest, readMetadata) {
  assert.ok(bytes.length >= header.size, 'truncated dynamic header');
  assert.equal(bytes.subarray(0, 8).toString('ascii'), 'PANDA\0\0\0');
  // This bounded oracle covers the three profiles in the fixed paired index.
  // Other profiles must extend this test adapter explicitly, never silently skip.
  assert.ok([[12, 0, 2, 0], [12, 0, 6, 0], [13, 0, 1, 0]].some(version =>
    version.every((value, index) => bytes[header.version + index] === value)), 'unsupported census profile');
  assert.equal(bytes.readUInt32LE(header.fileSize), bytes.length);
  const foreign = bytes.readUInt32LE(header.foreign), foreignSize = bytes.readUInt32LE(header.foreignSize);
  assert.ok(foreign + foreignSize <= bytes.length);
  const count = bytes.readUInt32LE(header.count), index = bytes.readUInt32LE(header.index);
  assert.ok(index >= header.size && index + count * wordBytes <= bytes.length, 'class index bounds');
  const raw = new Map();
  for (let slot = 0; slot < count; ++slot) {
    const entity = bytes.readUInt32LE(index + slot * wordBytes);
    if (entity >= foreign && entity < foreign + foreignSize) continue;
    assert.ok(entity >= header.size && entity < bytes.length, 'class offset bounds');
    assert.ok(!raw.has(entity), 'duplicate class index');
    let cursor = entity;
    const uleb = () => {
      let value = 0, factor = 1;
      for (let size = 0; size < maxLebBytes; ++size) {
        assert.ok(cursor < bytes.length, 'truncated class header');
        const byte = bytes[cursor++];
        value += (byte & lebPayload) * factor;
        if (!(byte & lebContinuation)) {
          assert.ok(value <= 0xffffffff, 'ULEB overflow');
          return value;
        }
        factor *= 2 ** lebBits;
      }
      assert.fail('unterminated ULEB');
    };
    uleb(); // MUTF-8 length/flags; byte extent ends at the null terminator.
    const terminator = bytes.indexOf(0, cursor);
    assert.ok(terminator >= cursor && terminator + 1 + wordBytes <= bytes.length, 'class descriptor bounds');
    cursor = terminator + 1 + wordBytes; // descriptor, superclass entity
    uleb(); // access flags
    raw.set(entity, {fields: uleb(), methods: uleb()});
  }
  assert.equal(manifest.records.length, raw.size, 'record census');
  const seen = new Set();
  const totals = {records: raw.size, methods: 0, fields: 0, noModule: 0};
  for (const row of manifest.records) {
    assert.ok(raw.has(row.entity) && !seen.has(row.entity), 'record identity census');
    seen.add(row.entity);
    const counts = raw.get(row.entity), metadata = readMetadata(row);
    assert.equal(metadata.entity, row.entity, 'metadata owner');
    assert.equal(metadata.methods.length, counts.methods, 'method census');
    assert.equal(metadata.record_fields.length, counts.fields, 'field census');
    totals.methods += counts.methods; totals.fields += counts.fields;
    if (!row.source) {
      assert.equal(row.source_status, noModuleStatus, 'missing source status');
      assert.equal(counts.methods, 0, 'record with methods cannot be accepted without source');
      assert.equal(metadata.module, null, 'module cannot be accepted without source');
      ++totals.noModule;
    }
  }
  assert.equal(manifest.counts.records, totals.records);
  assert.equal(manifest.counts.methods, totals.methods);
  assert.equal(manifest.counts.record_fields, totals.fields);
  return totals;
}
