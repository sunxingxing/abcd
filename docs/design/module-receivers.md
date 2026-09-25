# Module Receiver Expressions

FoldModuleReceivers connects an already-proven module load to a staged member
call's receiver. It operates on shared typed AST nodes, not method names or
source fragments. Normal SSA, no catch region and no branch argument region
are required.

The load and property read must be in the same basic block. Only unmerged
NoOperation statements with impossible exceptions and no language effects, or
the immediately following certified module Hole guard, may separate them. The
module assignment must still target the accumulator and retain its unchanged
semantic ModuleLoad origin. Extra uses and effectful separators retain the old
form.

The initial direct-definition rule did not affect pasteboard: Logger's module
load at PC 12 is followed by a proven guard at PC 14, an elided receiver save
at PC 18 and reload at PC 20, then the property read at PC 22. HasReceiverUse
therefore traces only that typed, already-elided transport: the guard must
carry the module's previous_origin certificate and read its exact SSA value;
each adjacent save/reload pair must be pure and normally completing, with
exact source/target identities and sole-use definitions. After accounting for
the guard, the current value must have exactly one use at each transfer and
finally at the property read. No raw use count is mutated by this pass.

The module expression and original InstructionId become a ModuleReceiver on
SourceStagedCall. The old assignment becomes a zero-width NoOperation. Native
module binding emission and all existing scope/import/export checks remain
mandatory. The live binding is evaluated exactly once, before getter lookup
and arguments, across no observable operation. This does not freeze imports
or assume that module exports are immutable.

Before emitting child arguments, the printer validates the original load PC's
span via its instruction distance and rebases it onto the actual module
receiver expression. An attached proven guard retains the same rebased span.
The property and call spans retain their existing owner.
Nested staged nodes stop the same-block candidate scan; candidates entirely
inside an argument interval use the same local proof. No text-based source-map
reconstruction or dedicated record output path is introduced.

Structural work is O(I); existing normal-SSA lookups across the disjoint
candidate intervals give O(I log(I + U)) worst-case time for U reads.
Scratch is O(1); the typed optional node is covered by
the existing sizeof-based AST admission. Independent read-only review found
no concrete correctness issue.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include mutable module bindings, throwing getters, extra SSA/phi
uses, effectful separators, nested arguments, malformed source distances,
binding-admission refusal, phased fallback, and exact generated positions.

## Production Evidence

Both production targets built successfully. The first ABC-only export,
`build/pasteboard-module-receivers-01`, was unchanged and led to inspection of
Logger method 4658's actual guard/transport chain. After adding that proof,
`build/pasteboard-module-receivers-02` totals 8,092 lines / 140,305 bytes versus
8,118 / 140,675 before this change. Six record sources changed. Logger's four
logging methods now contain direct module-member calls with native field/rest
arguments and no VM temporary declarations.

The exported map for method 4658 places PCs 12 and 14 on bytes [391,399), the
module identifier. PCs 22 and 70 cover bytes [391,452), the full call statement.
Global partial-map counts remain 195 methods / 9,745 spans. Record statuses
remain eight incomplete, two refused constructors at PC 14, and nine metadata.
The expanded proof received independent read-only review with no concrete
issue. No tests, generated JS execution or RSS checks were run; complete
behavioral equivalence remains unproven.
