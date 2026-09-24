# Structure-First Record Output

## Priority and Deliverables

User priority: record paths and file names, imports, class fields/frameworks,
global/local bindings, async/function names and signatures, then function bodies.
This supersedes continued body-opcode coverage work. Final completion still
requires semantically correct JS/TS/ArkTS per record, not metadata alone.

First executable increment: `abc_structure INPUT.abc OUTPUT_DIR` writes a fresh
output tree containing one declaration view and one metadata sidecar per record,
plus a manifest mapping original descriptor/entity to paths. The `.d.ts` files
are explicitly structural declarations, not recovered executable implementations.
Do not invent function bodies or top-level bindings for every method table entry.

For diagnostic artifacts, use descriptor path segments with deterministic escaping and a record entity-ID
suffix, preserving the original descriptor in metadata. Reject overlong paths
explicitly; never interpret `..`, absolute prefixes or separators as host path
authority. An existing destination is never overwritten. Stage output privately
and publish atomically only after all records and the input identity validate.

## Evidence Boundaries

- Module requests/imports/namespace imports/local exports/indirect exports/star
  exports come from the parsed module entity. Declaration names may be generated
  for compiler-local bindings; preserve original textual names in the sidecar.
  Import bindings and module-owned local slots are separate domains. In current
  supported profiles (> runtime OLD_VERSION 0.0.0.2), adjacent equal local-export
  names share a slot; nonadjacent equal names do not. A regular import's `*`
  name denotes namespace import, but an indirect export's `*` is a literal name.
- Preserve encoded method names and runtime-parsed names separately. Initial
  FunctionKind gives async/generator/arrow evidence, not final lexical placement.
- Report callType/frame slots as ABI signature evidence. Do not infer rest,
  defaults, parameter names, TS types, return types or export-to-method mapping
  from argument count or similar names.
- Executable source may use a debug local as a simple formal name only after
  an entry ABI-copy and scope-start proof. This does not turn debug `any` into
  an original TS type or establish defaults/rest syntax.
- Retain method name/debug-info entity references in MetadataIndex. Debug name
  and scope parsing follows this increment; absence is not fabricated metadata.
- Raw record fields are metadata, not proven source class fields. Class-buffer
  decoding/creation sites and initialization stores must establish membership,
  static/instance distinction, constructor and inheritance before class output.
- Lexical/global stores and debug scopes establish binding names/lifetimes.
  Do not expose a flat register inventory as source local variables.
- Bundle module selection requires an explicit runtime descriptor when special
  module records depend on it. Host input path is not that descriptor.

## Executable Module Export Boundary

Pinned v7 `STMODULEVAR` writes an accumulator value to a numeric local-export
slot and preserves the accumulator. `LiftModuleStore` accepts narrow/wide
encodings only; the deprecated name-based operation remains separate. The
source slot plan follows the runtime's adjacent-equal local-name rule, checks
duplicate exported names, and emits static ESM export aliases to private
`$localN` bindings. At a caller-proven top-level, straight-line first store it
emits `let $localN = $acc`; later stores remain assignments. Thus declaration
timing is not moved ahead of the bytecode write, and aliases stay live.

`WriteRecordProgram` now completes one narrow executable record shape: pinned
v7 merged modules with a unique `func_main_0`, a straight-line entry, pure
ABI/scalar copies, Hole/undefined, admitted ordinary class/function creation, a
pre-escape own `prototype` read, and local export stores. It interprets values
with typed states rather than printing instruction names, emits each class at
its creation point, and rejects all unknown side effects and incomplete slots.
Both the complete module and every created class are preflighted before any
source byte reaches the caller. The paired Breakpoint record executes under
Node with the recovered four instance assignments and branching method.
Top-level `DEFINEFUNC` now retains creation order and exports the admitted
function body/signature. The ordinary and simple async-function cases execute
under Node; `DEFINEMETHOD` home-object behavior and unhandled async-body
opcodes remain refusal cases.
Guarded v7 external-module loads now take a read-time snapshot into a private
binding before any later register copy or export. Only a valid import slot with
an immediately adjacent static-name Hole guard is admitted; the guard name is
not eagerly read because the pinned runtime resolves it only on the exceptional
Hole path. A Node oracle checks that later changes to the live import do not
change the exported copy. Derived classes using that import as heritage still
refuse executable source until constructor and `super` semantics are proved.

`abc_structure INPUT.abc OUTPUT_DIR --source` now adds a `.mjs` file at the
record path only when the whole-file role proof, record-owned module context,
and `WriteRecordProgram` all succeed. It retains the `.d.ts` and metadata
files for every record. The manifest uses view
`declarations_with_verified_source`; each record has a numeric `source_status`:
0 written, 1 module-context refusal, 2 no module entity, 3 program refusal.
Program refusal also records `source_error` as `RecordProgramError`'s enum
ordinal and `source_pc`; module-context refusal records the reason variant
index. These are diagnostics, not source semantics. Unsupported records have
no `source` path, and failed staged `.mjs` files are deleted. A resource,
read, write, or file-identity failure aborts publication of the whole tree.

This is still narrow record coverage, not completion of the user gate. The
same-file record ownership and whole-file role proof are supplied by the CLI;
unproved branches, reentrant/cyclic reads, unsupported bodies, indirect/star
exports, historical execution semantics and bundle ownership remain explicit
refusal cases, not partial executable files.

## Staged Implementation

1. Tests first: safe deterministic record paths, duplicate descriptors, traversal
   strings, bounded lengths; actual per-record files from module fixture.
2. Add method name/debug references and regression assertions to the metadata
   index; preserve existing tag validation and budgets.
3. Implement typed path planning and bounded streaming record JSON/declaration
   writers. Decode strings one at a time, module payload one record at a time,
   method callType/frame one method at a time. No instruction-body decoding.
4. Add fresh-tree publication CLI and manifest; tests for import/export mapping,
   kind/signature evidence, static refusal and destination overwrite prevention.
5. Run paired ABCs and inspect output names and module declarations. Compare
   record/method/import/export counts against parsed metadata; measure RSS under
   the 1,000,000,000-byte address-space ceiling. Retain static corpus cache.
6. Next increments: source/debug tables and proto signatures; class literal
   layouts and constructor/method ownership; global/module/local binding
   analysis; then enrich declaration files with proven framework information.
   Function-body expansion stays behind these structural tasks.

The first CLI is synchronous record export, not the required final async/parallel
function task service. Memory admission includes metadata index, current module,
current string and buffered file IO; output is streamed. Work is linear in
records/fields/methods/module entries apart from existing indexed lookups and
path handling. Import-name validation uses an ordered symbol table, with
O(I log I) comparisons and name comparison costs for I imports. Local-export
slot numbering is streaming and linear in decoded name bytes. No quadratic
name search or whole-corpus JSON DOM is permitted.
