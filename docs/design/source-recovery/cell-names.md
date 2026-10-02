# Named record cells

Record roots recover identifier hints from the pinned dynamic
NEWLEXENVWITHNAME scope literal format, already validated by `named_scope.cc`:
a leading integer followed by string/slot pairs. This is ABC metadata only;
paired source never supplies names to the engine. Record fields and register
slots are not treated as source variables.

The shared resolver groups allocation sites by resolved literal identity and
reads each scope array once. It sorts slot entries, omits ambiguous slots and
identifiers that cannot be emitted safely, and appends allocation/slot/NameId
hints. `globalThis` is reserved by the existing source contract. The allocation
OpId and LexicalSlot still establish cell identity; spelling establishes none of
storage, initialization, lifetime, constness or capture ownership.

FunctionTree construction checks hint ranges/order and reuses a metadata name
only when it is unique in the reserved name arena. Existing parameters, module
aliases, captures and textual references therefore take precedence. Duplicate
spellings retain generated names. Class/descendant capture preparation then
checks the actual parent spelling through the existing ownership and global
visibility checks.

A failed name-bearing record can make one fresh attempt with generated cell
names. The first unprinted tree has already been destroyed. This is the same
value-tree implementation and the same semantic/publication gates, not a legacy
writer or a looser semantic route. Read, file-change, memory, work and output
failures are not retried. The retry is bounded even when a semantic failure is
unrelated to naming. A descendant global-name collision test verifies that
metadata never changes a global access into a local capture.

For M named allocations, K entries across distinct scope literals and H output
hints, grouping/sorting costs O(M log M + K log K + H log H), plus pool lookup,
string validation and output copying. Binding lookup uses sorted indexes.
Shared literals do not cause a per-cell graph walk; copies proportional to H
are actual output. Scratch and vector-growth overlap are admitted against the
existing byte budget; work is charged. The one retry adds at most a constant
factor. This uses standard-library sorting and binary search, no dependency.
Wire-format maintenance follows the pinned ISA/runtime scope metadata and the
existing named-scope validator.

Tests cover alias pool slots, invalid names, ambiguous/out-of-range slots,
work limits, 200/201 and geometric sizes; decoded class-only record fixtures
cover parent/capture spelling and descendant global shadowing. Real record
checks recover DOMAIN, KEY_TEXTAREA, DATA_KEY and other metadata names. Token
comparison verifies presentation changes only; it is not itself semantic proof.
Scope verification and independent paired behavior checks remain separate.

Current integration is record-root naming. Ordinary function-local metadata
naming, const reconstruction, constructor gaps, general arithmetic domains and
complete platform/source-style acceptance remain separate unfinished work.
