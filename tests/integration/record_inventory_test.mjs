import assert from 'node:assert/strict';
import {verifyRecordInventory} from './record_inventory.mjs';

// Hand-encoded class headers, independent of the production writer. The oracle
// only reads header counts; this fixture intentionally has no method bodies.
const bytes = Buffer.alloc(512);
bytes.write('PANDA\0\0\0'); bytes.set([12, 0, 6, 0], 12);
bytes.writeUInt32LE(bytes.length, 16);
bytes.writeUInt32LE(3, 28); bytes.writeUInt32LE(60, 32);
function record(slot, offset, fields, methods) {
  bytes.writeUInt32LE(offset, 60 + slot * 4);
  const descriptor = Buffer.from('L任意;\0');
  bytes[offset] = 8; descriptor.copy(bytes, offset + 1);
  let position = offset + 1 + descriptor.length;
  bytes.writeUInt32LE(0, position); position += 4;
  bytes[position++] = 0; bytes[position++] = fields;
  if (methods >= 128) {bytes[position++] = (methods % 128) + 128; methods = Math.floor(methods / 128);}
  bytes[position] = methods;
}
record(0, 80, 0, 0); record(1, 120, 1, 0); record(2, 160, 0, 201);
const metadata = new Map([
  [80, {entity: 80, module: null, record_fields: [], methods: []}],
  [120, {entity: 120, module: null, record_fields: [{}], methods: []}],
  [160, {entity: 160, module: {}, record_fields: [], methods: Array.from({length: 201}, (_, entity) => ({entity}))}],
]);
const manifest = {records: [
  {entity: 80, source_status: 2}, {entity: 120, source_status: 2},
  {entity: 160, source: 'module.mjs', source_status: 0},
], counts: {records: 3, methods: 201, record_fields: 1}};
const verify = (input = bytes, listing = manifest, records = metadata) =>
  verifyRecordInventory(input, listing, row => records.get(row.entity));
assert.deepEqual(verify(), {records: 3, methods: 201, fields: 1, noModule: 2});
assert.deepEqual(verify(bytes, {...manifest, records: [...manifest.records].reverse()}), verify());
let rejected = 0;
for (const change of [
  listing => {listing.records.pop();},
  listing => {listing.records[0] = listing.records[1];},
  listing => {delete listing.records[2].source; listing.records[2].source_status = 2;},
  listing => {listing.records[0].source_status = 3;},
  listing => {listing.counts.methods = 200;},
]) {
  const listing = structuredClone(manifest); change(listing);
  assert.throws(() => verify(bytes, listing), {name: 'AssertionError'}); ++rejected;
}
for (const change of [
  records => {records.get(160).methods.pop();},
  records => {records.get(120).record_fields = [];},
  records => {records.get(80).module = {};},
  records => {records.get(80).entity = 120;},
]) {
  const records = structuredClone(metadata); change(records);
  assert.throws(() => verify(bytes, manifest, records), {name: 'AssertionError'}); ++rejected;
}
// A false metadata empty-method list cannot hide a raw declaration of one method.
const changed = Buffer.from(bytes); changed[80 + 1 + Buffer.byteLength('L任意;\0') + 6] = 1;
assert.throws(() => verify(changed), {name: 'AssertionError'}); ++rejected;
assert.throws(() => verify(bytes.subarray(0, 30)), {name: 'AssertionError'}); ++rejected;
// Foreign class-index entries are references, not local records to decompile.
const foreign = Buffer.from(bytes);
foreign.writeUInt32LE(400, 20); foreign.writeUInt32LE(40, 24);
foreign.writeUInt32LE(4, 28); foreign.writeUInt32LE(400, 72);
assert.deepEqual(verify(foreign), verify());
console.log(JSON.stringify({rejected, recordOrderIndependent: true, multiByteCounts: true, foreignReferences: true}));
