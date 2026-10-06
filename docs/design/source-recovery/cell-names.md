# Named lexical cells

Record roots and every ordinary/phased callable recover identifier hints from the pinned dynamic
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

Rules 130 connects the same resolver to `ResolveFunctionSyntax`, after formal
and rest ownership resolution. Class methods, created functions and their
children therefore receive allocation/slot hints through their existing family
adapters. The metadata is included in the same name arena, byte/work budget and
file identity checks. Rules 132 adds the naming plan below before any ordinary
family member is constructed. The record-root adapter keeps its existing bounded
retry for its separately seeded root; an ordinary family no longer needs a
retry to avoid ancestor naming hints shadowing descendant global/module names.

Decoded normal/arrow family fixtures cover local storage and nested writes,
reserved identifiers and collisions with the actual callable name, including
retained family printing after file closure. Existing resolver tests cover
200/201 and geometric growth. Independent pasteboard method behavior compares
240 value/callee-mutation/exception scenarios after ABC-only generation, and
detects a valid-JS mutation that replaces the captured parameter initialization.
This tests the recovered method; it is not a whole-platform behavior claim.

A captured storage name does not establish a source formal name. Missing debug/formal evidence, const reconstruction and complete
platform/source-style acceptance remain unfinished. Setter-capable constructor assignments remain
assignments; naming a captured receiver cannot prove native class fields.


## Family reference reservations (rules 132)

Shared syntax/constants and native module aliases are prepared once for every
normal or phased family occurrence after the existing default projection and
rebinding. Live typed ValueGlobalRead operations and actual ModuleSyntax aliases
supply textual reference names. No sample, source fragment, opcode sequence,
register layout or producer whitelist supplies a naming decision.

An owned name/owner-sorted index records those references. Explicit incoming
family edges and the reader's preorder occurrence contract define each subtree;
reverse subtree maxima take linear work. Each tree builder borrows the index
only during synchronous construction. It queries the current occurrence range
for preferred lexical hints, metadata formal hints and generated binding names.
Conflicting preferences keep an independently generated name; generated names
use their existing monotone fallback namespace while querying the same index.
No index pointer survives in a constructed or published tree. Incoming captures
retain the actual parent spelling and binding identity.

This is a conservative naming policy at callable-subtree granularity: it may
omit a hint that would be safe in a narrower block. It never rejects output for
that choice. Sibling occurrence ranges remain separate. The existing actual
creation-scope Euler analysis independently verifies globals, modules, captures,
TDZ and writes; it is not replaced by the naming index. External destination
bindings and seeded record roots still need their existing independent checks.

The tree route validates source identifier syntax through IsSourceIdentifier,
including legal dollar names and ClassValue/MethodSelf-prefixed names. These are
textual boundaries, not semantic tags. Legacy AllowsGlobalReference private
namespace exclusions remain in legacy writers and older naming adapters; they
are not tree-language invariants. ASCII/restricted-identifier coverage remains
explicit. A recovered debug formal is still only evidence for its ordinal name;
a naming preference proves none of its value/storage/default ownership.

For F occurrences, G reference sites and Q naming queries, additional work is
O(F + N + G log G + Q log G) excluding string comparison lengths; N is the live
operation census. Syntax/module preparation reuses the original adapters rather
than re-resolving every reference per ancestor. Additional storage is cached
syntax/aliases plus O(F + G) indexed references and their owned text. Arena
replacement, strings, vector capacity and sorting/query work are charged to the
existing conservative family byte/work budget. There is no F-by-G matrix or
per-variable graph walk. Standard sorting/binary search and preorder subtree
indexing add no dependency. Unit lookups cover 200/201/402/804 sites, sparse
owners, empty ranges, UINT32_MAX endpoints and callback work exhaustion; this
is operation-count evidence, not a resumed benchmark/RSS certification.

Forty-eight real encoded fixtures cover global/module references, metadata and
unnamed allocations, split blocks/register copies, and legal former private
names. The checked PreparedFamily prints with its actual destination; an external
shadow still refuses without output. Independent generated-JS execution checks
1032 identity/live-binding/getter/call/exception scenarios, fresh closures and
native arity/runtime names, and detects 48 valid-JS mutations to initialization
or binding shadowing. The Node global getter is installed in the realm, not on
the sandbox object; throwing-getter traces compare to an independent native
function in the same realm rather than hardcoding contextified lookup behavior.
This does not certify platform global-object implementation details or an entire
record. Fixed five-input/six-mode output is unchanged from rules 131.
