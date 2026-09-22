// Validation-only dependency: TypeScript 5.9.3. No sidecar source is input.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const ts = require(path.resolve(process.argv[2]));
assert.equal(ts.version, '5.9.3');
const totals = { records: 0, methods: 0, record_fields: 0,
  module_requests: 0, module_imports: 0, module_exports: 0 };
const debugTotals = { parsed: 0, failed: 0, locals: 0, positions: 0, source_files: 0 };
const classTotals = { sites: 0, failed: 0, scan_failed: 0, prototype: 0, static: 0, methods: 0 };
for (const root of process.argv.slice(3)) {
  const manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json')));
  const methods = new Map();
  for (const entry of manifest.records) {
    const record = JSON.parse(fs.readFileSync(path.join(root, entry.metadata)));
    for (const method of record.methods) methods.set(method.entity, record.entity);
  }
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
    for (const method of record.methods) {
      assert(Object.hasOwn(method, 'class_sites'));
      if (method.class_scan_error) classTotals.scan_failed++;
      for (const site of method.class_sites ?? []) {
        classTotals.sites++;
        if (site.error) { classTotals.failed++; continue; }
        assert(methods.has(site.constructor));
        assert(Number.isInteger(site.constructor_length));
        for (const property of site.properties) {
          assert(['prototype', 'static'].includes(property.placement));
          classTotals[property.placement]++;
          if (property.method) {
            classTotals.methods++;
            assert.equal(methods.get(property.method.entity), property.method.record);
          }
        }
      }
      assert(Object.hasOwn(method, 'debug'));
      if (method.debug_info === null) assert.equal(method.debug, null);
      else {
        assert(Array.isArray(method.debug.parameter_names));
        assert(Number.isInteger(method.debug.line_start));
        assert(Number.isInteger(method.debug.line_program));
        assert(Object.hasOwn(method.debug, 'scopes'));
        if (method.debug.scopes) {
          debugTotals.parsed++;
          debugTotals.locals += method.debug.scopes.locals.length;
          debugTotals.positions += method.debug.scopes.positions.length;
          if (method.debug.scopes.terminal_file !== null) debugTotals.source_files++;
          assert(Array.isArray(method.debug.scopes.locals));
          assert(Array.isArray(method.debug.scopes.positions));
          for (const local of method.debug.scopes.locals) {
            assert(Number.isInteger(local.register));
            assert(local.end === null || local.end >= local.start);
          }
        }
        if (method.debug.scope_error) debugTotals.failed++;
      }
    }
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
console.log(JSON.stringify({ typescript: ts.version, counts: totals, debug: debugTotals, classes: classTotals }));
