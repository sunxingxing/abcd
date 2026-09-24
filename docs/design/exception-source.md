# Exception Source Integration

## Current Boundary

The common semantic path supports explicit `THROW_PREF_NONE` as
`ThrowOperation`, with `ExceptionalCompletion::kAlwaysThrow`. The pinned
OpenHarmony-v7.0-Release interpreter handler in `interpreter-inl.cpp` passes
the accumulator to `SlowRuntimeStub::Throw` and jumps to exception dispatch.
The operation performs no coercion or normal register write. Its existing
register-read effect is checked by SSA Language-value verification; Hole and
unproven internal values are not emitted as JS exceptions.

Normal CFG construction already gives throw no successor. Region construction
therefore retains it as a terminal block, and `SourceThrow` prints the original
value with the instruction's original-PC provenance. Other THROW* opcodes are
not aliases: typed runtime error construction requires separate lifting.
The new semantic and exceptional-completion alternatives are appended to
preserve existing variant/enum indices.

This does not admit methods with exception tables. The common builder still
rejects those methods rather than silently dropping handlers. The async
normalizer only removes a proven single implicit catch/reject wrapper.

## Remaining Integration

1. Use the existing bounded `BuildExceptionDispatch` index, which preserves
   encoded handler priority. Do not sort overlapping try regions and thereby
   change selected handlers.
2. Extend the semantic CFG with typed exceptional edges from potentially or
   definitely throwing operations. A handler sees the throwing instruction's
   committed register/environment state, not its normal-path destination.
   The thrown accumulator value needs an explicit catch-entry definition.
3. Feed those edges and definitions through SSA and environment analysis.
   Normal and exceptional continuations must not share an optimistic value
   fact. Account for the expanded graph and actual phi/dependency counts.
4. Recover typed try/catch regions only after entry, exit and dispatch proofs.
   Catch bodies may mutate or inspect existing registers and lexical cells;
   they cannot be analyzed as unrelated standalone functions. Preserve throws
   from inside a catch and abrupt exits across enclosing regions.
5. Let async normalization remove only its outer compiler wrapper while
   retaining inner exception tables, rebased boundaries and original-PC
   mappings. Native await rejection must reach the same inner handler as the
   original resume/rethrow sequence. No rounding of deleted boundaries.
6. Lower these regions through the common AST/printer and then record owners.
   Keep current proven no-handler paths as a baseline, not a second exception
   semantics implementation.

Actual pasteboard method 9165 has two regions: `[40, 992)` dispatches to the
handler at 994 (length 5), and the outer async `[18, 1003)` dispatches to 1003
(length 5). This was inspected from the decoded production body in the
debugger. Removing both regions would suppress observable inner handling and
is not an acceptable shortcut.

## Verification Status

Both CLI targets compiled after explicit-throw integration. ABC-only survey
`build/pasteboard-semantic-throw.jsonl` retains stage counts
`[0,0,27,1,0,8,0,11,113,56,3]`; this prerequisite does not increase complete
methods or records. Read-only review found no concrete defect. Tests and RSS
collection remain paused by request. Deferred regression cases include thrown
primitive/object identity, uninitialized/Hole refusal, malformed operands,
conditional throw/return paths, loop exits and original-PC spans. Exception
edge state, handler priority and nested async rejection require additional
coverage when those layers are implemented.
