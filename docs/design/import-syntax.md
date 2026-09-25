# Import Syntax Recovery

The shared module writer classifies decoded wire imported names into named,
default and namespace forms using an exhaustive typed enum. The reserved
wire labels `default` and `*` are recognized only at this format boundary.
Default imports now use `import $importN from ...`, which retains the same
live binding as `import { "default" as $importN } from ...`.

Named imports and indirect exports print ASCII IdentifierName keys without
quotes. ModuleExportName permits keywords, unlike local binding identifiers.
Non-ASCII, empty and punctuation names retain exact string-literal escaping.
Default reexports remain inside braces; namespace reexports retain their
separate form. Local import aliases, module specifiers, slot indices and
indirect export resolution do not change.

Local export aliases use the same IdentifierName classifier. For example,
`export { $local0 as PasteBoard }` and `export { $local0 as default }` retain
their original live bindings; they do not become `export default` value
snapshots. Special/empty/non-ASCII exported names keep string-literal escaping.
This spelling is not used to infer a private local name. The export writer's
dry run still validates the complete output before publication, with each
emitted byte charged to the original output budget and no new allocation.

The writer now builds a typed directive index for regular and namespace
imports, sorted by request ID, kind and original entry index. Requests are
visited in ABC order, emitting their imports with original binding-slot aliases.
Only requests without import bindings retain a request-only declaration.
Import-slot order is not necessarily request order (PasteBoard is an example);
the index preserves dependency traversal without duplicate bare imports.
Indirect/star reexports still follow in their original table order; requests
used only by reexports keep their bare declaration. Original local-name
recovery still requires shared scope/collision proofs.

For E import entries and R requests, planning takes O(E log E + R), with O(E)
temporary storage. Count accumulation and actual vector capacity are checked
against the caller-admitted temporary string allowance; decoding receives the
remaining allowance while the index lives. There is no quadratic scan per
request or name cache. Classification and printing remain linear in textual
data, with the existing output budget and buffered semantic-failure boundary.
Request-only modules need no directive allocation. Invalid import request IDs
are rejected before emission; reexport failures also leave output untouched.

## Evidence

Local export spelling builds in both production CLIs; read-only review found
no concrete defect. ABC-only `build/pasteboard-export-names-01` publishes the
same eight incomplete records and rejects the same two constructors at PC14.
Thirteen local export aliases use IdentifierName spelling, including Logger's
default export and PasteBoard's two exports. This is not runtime verification;
export spelling golden updates and keyword/escaping/budget regressions remain
deferred with the paused test work.

Request-ordered emission builds in both production CLIs; read-only review found
no concrete defect. ABC-only export `build/pasteboard-request-imports-01`
retains the eight incomplete records and two constructor refusals. It removes
22 duplicate import lines across published records, yielding 9,696 lines /
164,171 bytes. PasteBoard's request order is pasteboard, multimedia.image,
CommonConstants, hilog, while their aliases remain slots 3, 2, 0, 1 respectively.
Reexport order and original record paths remain unchanged.

Earlier syntax-only evidence follows:

Both production CLIs build; read-only review found no concrete defect.
ABC-only export to `build/pasteboard-import-syntax-01` retains eight incomplete
application records and the two host-constructor refusals at PC14. PasteBoard
prints `import { CommonConstants as $import0 } ...` and default imports for
hilog, image and pasteboard, retaining the preceding request-order declarations.

Tests, generated JS execution and RSS measurements remain paused. Deferred
checks cover default live bindings, namespace imports, default and namespace
reexports, keyword/empty/Unicode keys, dependency evaluation order and output
exhaustion. Export evidence does not establish runtime equivalence, original
local names or completion of the overall source recovery goal.

Additional deferred checks for request-ordered emission include mismatched
slot/request order, cyclic dependencies, interleaved request-only imports,
duplicate specifiers, reexport-only requests, invalid request IDs, zero
entries, temporary-budget exhaustion and 200/201 scaling. Dependency order
follows the first ESM request occurrence; declaration spelling does not infer
source local names or erase distinct binding slots.
