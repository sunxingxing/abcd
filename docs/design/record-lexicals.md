# Record lexical environments

The record entry interpreter now consumes `LiftLexicalInstruction` results,
using the same typed depth/slot and push/pop model as function analysis.
`ValidateNamedScopes` checks every named scope's literal and string references
before interpretation. Debug names are not evidence of source declarations.

## Admission boundary

This is a record integration prerequisite, not a replacement for the common
semantic IR, SSA, region AST or template capture machinery. The existing record
fast path admits sequential environment operations only: it rejects backedges
and arbitrary branches, and its two exact forward region matchers contain no
lexical operations. Class creation is flushed in bytecode order before lexical
operations, using the existing class admission checks.

Each frame owns a segment in a flat symbolic cell vector. New cells contain
Hole. Push and current-environment loads produce a typed internal identity,
not a JS object or undefined. Pop preserves the accumulator. Depth and slot
bounds are checked; accesses into the unknown incoming environment fail.
Registers can retain internal identities, but source value emission rejects
them. Loads copy the symbolic value at the original instruction, not a mutable
source expression evaluated later.

No JS lexical declarations are emitted yet. This is valid only while every
created callable rejects unresolved outer captures: admitted callees cannot
observe or mutate these cells. Removing that restriction requires shared
capture binding, initialization/escape proofs and actual source cell storage;
it must not merely enable the existing function template printer. In particular,
Home stores its class in a cell after class creation. Initialization at a later
invocation cannot be assumed to hold at creation.

## Resource bounds

For I interpreted instructions, S total allocated slot initializations, D peak
local environment depth and L peak live slots, interpretation costs O(I + S)
amortized, with O(D + L) retained storage. Depth lookup is indexed, not a parent
walk. Geometric vector growth admits the full new allocation while the old
allocation is still charged; pop retains and continues charging capacities.
All storage uses the record's remaining budget. Named-scope validation has its
own existing bounded sort/work contract and releases scratch before emission.

## Current evidence

Both CLI targets build. An ABC-only production inspection of pasteboard
`modules.abc` (SHA256
`854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e`)
produced `build/pasteboard-source-record-lexical/manifest.json`:

- Home record 5521: instruction rejection at PC0 becomes class rejection at
  PC381, where the pending class is flushed. This is the entry failure site,
  not a diagnosis of the failing constructor/member instruction.
- ProgressBar record 9550: instruction rejection at PC0 becomes value rejection
  at PC30.
- Only CommonConstants and Logger remain complete source records.

No source sidecars were inputs. Tests, runtime equivalence checks and RSS
measurements remain paused by user request. Regression coverage still needed
when resumed: named and unnamed pushes, empty/nested frames, wide operands,
pop with saved environment identities, Hole propagation, invalid outer depth
and slot, captured-child refusal, and growth-budget boundaries.
