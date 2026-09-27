# Immutable Function-Family Preparation

`PreparedFamily::PrepareCreated` and `PrepareClass` join the file-backed family route to owned,
immutable AST storage. This is a preparation boundary, not a new body writer or
an executable/record-completion receipt. No public constructor accepts a tree,
`FamilyWriteFacts`, or a caller-supplied verification report.

## Mandatory Route

1. Require the caller's generated destination to be strict; check file stability.
2. Resolve the actual function-creation or class-member occurrence and its
   profile/name/ABI. Only the context boundary differs; both enter `Prepare`.
3. Read the complete supported family from ABC using the existing iterative
   discovery and original input/value route.
4. Run shared default projection, edge rebinding, control/evaluation, source AST,
   semantics, flow, ABI, exact lexical enclosure and whole-family store checks.
5. Require supported ordinary/arrow syntax (or the actual class-method root), strict members, retained ABI and no
   unresolved module aliases. Root captures still refuse in the shared route.
6. Check the complete actual destination through `VerifyFamilyDestination`.
7. Check file stability again, move the root creation context and complete family
   into `unique_ptr<const Storage>`, and release input/SSA readers and scratch.

`FamilyDestination` is a caller trust boundary. A null global context admits no
global reads; it is not a guessed empty record. Strictness is also an explicit
enclosing-code promise, not a directive injected into a non-simple signature.
No destination pointer is retained. File/index lifetimes are not extended by the
prepared owner; trees, names, origins, ABI and capture/flow records own storage.

## Printing and Remaining Gates

`PrintDraft` rechecks strictness and actual external bindings before writing
bytes, then invokes the same iterative `PrintFamilyDraft` used by diagnostics.
For a class root it selects `PrintClassFamilyDraft`, which uses the same printer
event loop and actual `ClassTreeContext` name/placement. Only the root header and
expression wrapper differ. Prototype/static placement is preserved; nested
creation expressions cannot acquire method syntax or a home object. The output
is a class-body fragment, never an object literal or detached ordinary function.
It retains that printer's original-PC origin DAG and per-owner generated ranges.
A changed enclosure cannot reuse the earlier destination permission. Failed
output must be discarded; partial output is not a completed artifact.

`PrintDraft` still returns a draft product, not `Printed/complete` coverage.
The separate [owned emission boundary](family-emission.md) consumes preparation,
rechecks typed destination grammar and source positions, and owns per-member
Printed/Complete receipts. Record/module initialization, broader callable
contexts and execution acceptance remain unfinished. Existing CLI drafts are
not silently upgraded, and no new capability is marked Validated.

## Bounds and Evidence

All fresh context, family input, AST passes, destination checks and storage share
one decreasing byte/work allowance. Admission conservatively retains released
scratch charges. `AllocatedBytes()` is therefore an upper preparation charge,
not exact retained capacity or process RSS. Existing family discovery's work
counter does not aggregate all upstream per-method passes; this interface does
not fix or conceal that known accounting limitation.

Production `abc_semantic` and `abc_structure` builds pass. Added file-backed
specifications cover simple/cell/nested-writer families, shared output, refusal
before writing under sloppy context, zero-byte admission, moving the owner and
printing after the input file is destroyed. These tests are uncompiled/unrun
under the user's testing pause; no generated JS, benchmark or RSS was executed.

Class-context specifications compare ordinary/default/object member output and
parameter ranges with the established standalone route, and reject a mismatched
root owner or a new shadowing destination before output. ABC-only Immersive 21082
now prints `setInitiallyProvidedValue(params)` as a class-member draft with 21
instruction dispositions. Pasteboard 7449 remains a four-member, 1434-byte
function-expression draft. Both retain incomplete/nonexecutable status.
