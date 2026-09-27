# Original Instruction Source Coverage

`BuildInstructionSources` closes a coverage bookkeeping gap between the verified
residual IR and source positions. Every original semantic instruction must have
one typed disposition in the unchanged function tree:

- `ExprId`, `StmtId`, `PropertyId`: represented by that syntax node.
- `AnchorId`: retained control/environment operation with a checked boundary.
- `ParameterId`: moved into a default signature by its separate proof.
- `ElidedInstruction`: a removed operation with checked `kElidedPure` origin;
  it has no generated range, not a fabricated zero-width statement.

This pass requires existing input/work, default-signature, semantic and flow
proofs. It does not prove an optimization or make an effect pure. A removed
retained origin must map to signature syntax; live operations cannot do so.
Body and signature placements cannot share an operation or silently omit one.
Original PC, optional pre-normalization PC, method identity and IR revisions
must match. Current stable one-origin-per-input-site IR is explicit admission;
future instruction splitting must extend the model rather than relax coverage.

## Algorithm

Create one optional target per work operation and one visited bit per input
site. Index body/signature placements once, rejecting duplicates and invalid
node IDs. Visit each work operation once, validate its origin against the
original input and actual tree, and fill the input-site-ordered result. Require
all original sites visited. Complexity is O(I + O + P), storage O(I + O), for
input instructions I, work operations O and placements P. There is no PC-sized
array, per-instruction graph walk, transitive origin expansion or JS parsing.
Capacity and work are charged to the shared family allowance.

## Integration

File-family construction runs the pass after body/signature semantics and ABI
checking. Each `FamilyMember` retains its table; `PreparedFamily` requires it
before freezing storage. Synthetic diagnostic fixtures may omit the table.
`FamilyTreeFailure` appends `InstructionSourceError` at variant index 23 without
renumbering earlier cases.

The shared position serializer emits `instruction_sources` in original input
order for file-backed family drafts. Each item links to the existing origin DAG
and directly uses printer-event coordinates for its typed target. Coordinates
are absolute UTF-8 offsets in the full draft. `elided_pure` has `range: null`;
control/environment anchors have genuine zero-width ranges. Default operations
map to the whole formal syntax, not a guessed body-wide range.

This is complete disposition accounting for an admitted function, not an
executable or record-completion receipt. Strict/global/module and callable
requirements and the other family proofs still apply. Exceptions/async and
unsupported operations are not skipped to manufacture coverage.

## Evidence

Production targets `abc_semantic` and `abc_structure` built successfully.
ABC-only pasteboard 7449 draft remains 1434 bytes, with 36/66/5/5 dispositions
for methods 7449/7089/7117/7477. Root dispositions include six default-parameter
entries; total pure-elided entries are 16/35/4/3. Immersive 21082 conditional
family still constructs. Both diagnostics retain incomplete/nonexecutable
status and exit 1. No generated JS was executed.

Missing/duplicate body placement, exact work-budget and CLI disposition/range
specifications were added but remain uncompiled/unrun under the testing pause.
