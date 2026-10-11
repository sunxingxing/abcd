import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {classReference, parseJavaScript} from './class_reference.mjs';
import {nodes, classMethodProfiles} from './record_style.mjs';
import {signatureShapes, verifySignatures} from './signature_shapes.mjs';
import {verifyCallableSignatures} from './callable_signatures.mjs';
import {verifyRecordInventory} from './record_inventory.mjs';
import {verifyModuleSurface} from './module_surface.mjs';

const [tool, root] = process.argv.slice(2);
const index = JSON.parse(readFileSync(new URL('../corpus/abc-index.json', import.meta.url)));
// Source-root mapping belongs only to the independent paired oracle.
const inputs = [
  ['Immersive/modules.abc', 'Immersive/Immersive_src', 'sources/'],
  ['MultiCommunityApplication/modules.abc', 'MultiCommunityApplication/src', 'sources/'],
  ['pasteboard/modules.abc', 'pasteboard/pasteboard_src', 'sources/entry/'],
  ['push-kit-sample-code-clientdemo-arkts/modules.abc', 'push-kit-sample-code-clientdemo-arkts/src', 'sources/'],
  ['push-kit-sample-code-clientdemo-arkts/widgets.abc', 'push-kit-sample-code-clientdemo-arkts/src_widget', 'sources/'],
];
assert.deepEqual(inputs.map(([path]) => path).sort(),
  index.entries.filter(row => row.corpus_id === 'paired').map(row => row.relative_path).sort());
const directory = mkdtempSync(join(tmpdir(), 'abcd-paired-signatures-')), records = [];
let noModule = 0, bytecodeMethods = 0, recordFields = 0;
try {
  // Finish every ABC-only exporter before reading any paired source.
  for (const [relative, paired, prefix] of inputs) {
    const identity = index.entries.find(row => row.corpus_id === 'paired' && row.relative_path === relative);
    const abc = join(root, relative), bytes = readFileSync(abc);
    assert.equal(bytes.length, identity.size);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), identity.sha256);
    const output = join(directory, relative);
    execFileSync(tool, [abc, output, '--value-source', '--language-numeric', '--language-constructors'], {timeout: 60_000});
    const manifest = JSON.parse(readFileSync(join(output, 'manifest.json')));
    assert.equal(manifest.sha256, identity.sha256);
    assert.equal(manifest.source_contract, 'language_numeric_constructors_v1');
    const methodCounts = new Map();
    const inventory = verifyRecordInventory(bytes, manifest, row => {
      const metadata = JSON.parse(readFileSync(join(output, row.metadata)));
      methodCounts.set(row.entity, metadata.methods.length); return metadata;
    });
    bytecodeMethods += inventory.methods; recordFields += inventory.fields;
    for (const row of manifest.records) {
      // External/native records have no module source; they are not silently
      // accepted as decompiled classes. Every actual source must be complete.
      assert.ok(!row.incomplete_source, `${relative}:${row.entity}`);
      assert.equal(row.source_route, 'value_tree');
      assert.equal(row.legacy_writer_used, false);
      if (!row.source) {
        // Serialized SourceStatus::kNoModule from tools/abc_structure.cc.
        const noModuleStatus = 2;
        assert.equal(row.source_status, noModuleStatus, `${relative}:${row.entity}`);
        ++noModule; continue;
      }
      assert.equal(row.source_complete, true);
      assert.ok(row.source.startsWith(prefix));
      records.push({label: `${relative}:${row.entity}`, bytecodeMethods: methodCounts.get(row.entity), source: readFileSync(join(output, row.source), 'utf8'),
        paired: join(root, paired, row.source.slice(prefix.length, -'.mjs'.length) + '.ts')});
    }
  }
} finally {rmSync(directory, {recursive: true, force: true});}
assert.equal(records.length, 106, 'fixed five-input source inventory');
assert.equal(noModule, 46, 'external/metadata records stay separate from source acceptance');
let classes = 0, methods = 0, functions = 0, defaults = 0, rest = 0, mutations = 0, callables = 0, callableMutations = 0, moduleMutations = 0, staticInitializers = 0;
for (const row of records) {
  // @Observed is the existing controlled decorator boundary. This checks
  // signatures only, not decorator invocation or arbitrary platform behavior.
  const reference = classReference(readFileSync(row.paired, 'utf8').replace(/^@Observed$/gm, '')).source;
  verifyModuleSurface(row.source, reference, row.label);
  assert.deepEqual(classMethodProfiles(row.source), classMethodProfiles(reference), 'native method representation ' + row.label);
  const module = parseJavaScript(row.source);
  const request = module.body.find(node => node.source);
  if (request) {
    const mutant = row.source.slice(0, request.source.start) + '"__wrong_module_request__"' + row.source.slice(request.source.end);
    parseJavaScript(mutant);
    assert.throws(() => verifyModuleSurface(mutant, reference, row.label), {name: 'AssertionError'}); ++moduleMutations;
  }
  const exported = module.body.find(node => node.type === 'ExportNamedDeclaration' && node.specifiers.length);
  if (exported) {
    const key = exported.specifiers[0].exported;
    const mutant = row.source.slice(0, key.start) + '"__wrong_public_export__"' + row.source.slice(key.end);
    parseJavaScript(mutant);
    assert.throws(() => verifyModuleSurface(mutant, reference, row.label), {name: 'AssertionError'}); ++moduleMutations;
  }
  verifySignatures(row.source, reference, row.label);
  verifyCallableSignatures(row.source, reference, row.label);
  const shape = signatureShapes(row.source);
  classes += shape.classes.length; functions += shape.functions.length;
  methods += shape.classes.reduce((total, value) => total + value.methods.length, 0);
  const params = [...shape.functions.flatMap(fn => fn.params), ...shape.classes.flatMap(value =>
    [...value.constructor, ...value.methods.flatMap(fn => fn.params)])];
  defaults += params.filter(value => value.type === 'AssignmentPattern').length;
  rest += params.filter(value => value.type === 'RestElement').length;
  // Mutate real emitted class signatures, independently selecting AST-owned
  // method parameters. The mutant must parse; syntax rejection is not evidence.
  for (const method of nodes(parseJavaScript(row.source)).filter(node => node.type === 'MethodDefinition')) {
    for (const param of method.value.params) {
      let begin, end, replacement;
      if (param.type === 'AssignmentPattern') {
        begin = param.right.start; end = param.right.end;
        replacement = param.right.type === 'Literal' && param.right.value === false ? 'true' : 'false';
      } else if (param.type === 'RestElement') {
        begin = param.start; end = param.argument.start; replacement = '';
      } else continue;
      const mutant = row.source.slice(0, begin) + replacement + row.source.slice(end);
      parseJavaScript(mutant);
      assert.throws(() => verifySignatures(mutant, reference, row.label), {name: 'AssertionError'});
      ++mutations;
    }
  }
  const all = nodes(parseJavaScript(row.source));
  const methodValues = new Set(all.filter(node => node.type === 'MethodDefinition').map(node => node.value));
  const nested = all.filter(node => ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type) && !methodValues.has(node));
  const staticClasses = all.filter(node => ['ClassDeclaration', 'ClassExpression'].includes(node.type) &&
    node.body.body.some(member => member.type === 'StaticBlock' || (member.type === 'PropertyDefinition' && member.static))).length;
  // Fixed paired compiler profiles emit one module entry, one constructor per
  // class (including an implicit constructor), and one static initializer per
  // class with static fields. This per-record accounting detects omissions;
  // signatures and independent execution still own identity/body equivalence.
  assert.equal(1 + shape.classes.length + shape.classes.reduce((count, value) => count + value.methods.length, 0) +
    nested.length + staticClasses, row.bytecodeMethods, 'method/source census ' + row.label);
  staticInitializers += staticClasses;
  for (const fn of nested) {
    ++callables;
    // Change an existing parameter without changing the body. Every counted
    // mutant must parse; zero-parameter/rest-only callables remain inventoried.
    if (fn.params.length === 0) continue;
    const param = fn.params[0];
    const replacement = param.type === 'AssignmentPattern' ? row.source.slice(param.left.start, param.left.end) + ' = false'
      : param.type === 'Identifier' ? row.source.slice(param.start, param.end) + ' = false' : undefined;
    if (!replacement) continue;
    const mutant = row.source.slice(0, param.start) + replacement + row.source.slice(param.end);
    parseJavaScript(mutant);
    assert.throws(() => verifyCallableSignatures(mutant, reference, row.label), {name: 'AssertionError'});
    ++callableMutations;
  }
}
assert.ok(mutations > 0);
console.log(JSON.stringify({inputs: inputs.length, records: records.length, noModule, bytecodeMethods, recordFields, classes, methods, functions, defaults, rest, mutations, callables, staticInitializers, callableMutations, moduleMutations,
  scope: 'raw record census, module interface and callable signatures; initializer/body behavior and platform execution checked separately'}));
