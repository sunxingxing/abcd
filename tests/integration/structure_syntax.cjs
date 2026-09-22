// Validation-only dependency: TypeScript 5.9.3. No sidecar source is input.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const ts = require(path.resolve(process.argv[2]));
assert.equal(ts.version, '5.9.3');
const totals = { records: 0, methods: 0, record_fields: 0,
  module_requests: 0, module_imports: 0, module_exports: 0 };
for (const root of process.argv.slice(3)) {
  const manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json')));
  const counts = Object.fromEntries(Object.keys(totals).map(key => [key, 0]));
  const paths = new Set();
  for (const entry of manifest.records) {
    for (const relative of [entry.metadata, entry.declarations]) {
      assert(!path.isAbsolute(relative));
      assert(!relative.split('/').includes('..'));
      assert(!paths.has(relative));
      paths.add(relative);
    }
    const record = JSON.parse(fs.readFileSync(path.join(root, entry.metadata)));
    assert.equal(record.entity, entry.entity);
    assert.equal(record.view, 'structure_only');
    const filename = path.join(root, entry.declarations);
    const source = fs.readFileSync(filename, 'utf8');
    const parsed = ts.createSourceFile(filename, source, ts.ScriptTarget.Latest, true, ts.ScriptKind.TS);
    assert.equal(parsed.parseDiagnostics.length, 0, filename);
    counts.records++;
    counts.methods += record.methods.length;
    counts.record_fields += record.record_fields.length;
    if (record.module) {
      counts.module_requests += record.module.requests.length;
      counts.module_imports += record.module.imports.length + record.module.namespace_imports.length;
      counts.module_exports += record.module.local_exports.length + record.module.indirect_exports.length + record.module.star_exports.length;
    }
  }
  assert.deepEqual(counts, manifest.counts);
  for (const key of Object.keys(totals)) totals[key] += counts[key];
}
console.log(JSON.stringify({ typescript: ts.version, counts: totals }));
