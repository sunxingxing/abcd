# Class Capture Binding

## Evidence and Design

ProgressBar method 10639 creates a local environment for `this` and an arrow at
PC24 (method 9804). Its descendants, including async 10333, also reference the
record's `dstDir` cell. The child template correctly carries depth1/slot0 into
10639; rebasing there yields incoming depth0/slot0. Previously class methods
unconditionally used reject mode and the class output stream could not carry
this obligation. This was a missing class ownership boundary, not proof that
the original ABC capture was invalid.

ClassExpression now carries an optional normal-SSA creation InstructionId.
The shared printer's class sink consumes typed capture tokens and uses the
existing creation environment, local initialization and capture-domain proof.
A local becomes the existing lexical source cell name; an incoming capture
remains a typed obligation for the enclosing owner. No debug name or printed
text selects the cell, and no standalone unresolved source is published.

The class emitter passes Language capture mode to methods, accessors and
attached methods only when the real sink supports typed capture tokens.
Preflight uses the same mode, never the dry sink's broader capability. Ordinary
public output and WriteClassMethod still default to rejection. Constructors
remain unchanged; this does not admit the rejected host superclass premise.

FoldClassAssembly permits no environment change or lexical write between class
creation and folded member creation. Thus their capture environment and cell
state can be checked at the common class site. The whitelist is part of this
proof boundary. Future folding across effects requires actual per-member
creation proofs. Phased classes have no normal creation coordinate and still
reject capture binding. Stronger Defined obligations retain their existing
guard proof; Language does not imply immutability or non-undefinedness.

## Cost and Verification Scope

Storage adds one optional instruction ID per class AST expression, already
accounted for by statement/variant allocation sizes. Binding reuses indexed
environment resolution and sorted cell/store/dominator facts; it does not scan
the parent once per capture or build another graph. Names are emitted through
the existing budgeted position cursor. Nested method maps still need separate
final-file relocation; the class creation span alone is not that mapping.

Read-only review found no concrete defect. Tests, generated JS execution and
RSS measurement remain paused. Deferred regressions include uninitialized
record cells, child writes, Defined captures, nested templates, environment
barriers during assembly, phased refusal, non-capture public streams and
preflight/output-budget consistency.

Both production CLIs built successfully. ABC-only export of unchanged
pasteboard `modules.abc` to untracked `build/pasteboard-class-captures-01`
now publishes eight incomplete app records. ProgressBar's recovered path is
`sources/entry/src/main/ets/pages/ProgressBar.incomplete.mjs`. Its nested async
functions bind record cells for dstDir, uiContext and VALUE_TEST_STRING_ELEMENT
to the parent `$lex0_0`, `$lex0_1` and `$lex0_2` bindings. No sidecar source or
source map was supplied to the engine.

EntryAbility and EntryBackupAbility remain refused at outer PC14; the rejected
superclass invariant was not relaxed. All eight published sources retain
`source_complete:false` and `executable:false`. This evidence demonstrates
record generation and resolved lexical references, not runtime equivalence,
full nested position mapping or source-style completion.
