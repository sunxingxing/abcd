# Read-Only Parameter Bindings

The callable owner may now reuse a formal parameter's already-validated name
for its ABI register when a whole-body may-write scan proves that register
is never written. This is binding recovery, not a rename inferred from an
opcode string or generated text. The common body/AST receives the alias through
ScalarNames, and the wrapper omits only that redundant register declaration.
When metadata names are unavailable the existing generated `$pN` name is used.

The first implementation admits only fixed parameter forms. Rest and neutral
default forms keep independent register bindings. Canonical frame validation
prevents overlap with function, new.target or this slots; those bindings are
unchanged. Every explicit register write, including unreachable instructions,
counts conservatively. Unknown access analysis yields no alias certificate.
Writable parameters retain their independently writable `$r` snapshot, never
assigning back into the original JS formal on a guess.

`FindParameterWrites` intersects decoded may-write windows with the parameter
range. Two difference events per window and one prefix scan compute coverage
without expanding overlapping ranges or rescanning the method per parameter.
The helper costs O(I+P), for instructions I and parameters P with specification-
bounded operand windows, and retains P+1 int64 counters. Overflow bounds,
actual vector capacity and work are checked; storage is deducted from the
callable budget and remains charged through constructor retry and emission.
Existing name validation/sorting costs are separate.

All names still pass strict binding, duplicate and enclosing-scope validation.
The owner emits strict functions (or class methods), and supported body paths
do not introduce mapped arguments or direct eval writes to formals. Nested
ABC closures access certified lexical cells, not writable aliases to frame
registers. The proof is about the checked body supplied by the owner, including
the normalized async body when applicable; it does not infer missing opcode
semantics or waive later body verification.

## Evidence And Limits

Both production CLIs build and read-only review found no concrete defect.
ABC-only export `build/pasteboard-parameter-aliases-01` retains eight incomplete
application records and two host-constructor refusals at PC14. Logger's
constructor directly reads `prefix` rather than declaring its ABI `$r9` copy.
Combined output decreases from 9,869 lines / 168,134 bytes to 9,725 lines /
165,189 bytes. Most local `$r` temporaries remain; this does not establish full
source-variable recovery or runtime equivalence.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include writable/read-only mixtures, writes on exception/unreachable paths,
overlapping/ranged writes, unknown access failure, zero/many parameters,
200/201 scaling, rest/default exclusion, strict arguments, normalized async,
constructor retries, nested shadowing, metadata-free names and budget limits.
