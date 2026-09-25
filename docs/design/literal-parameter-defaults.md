# Literal Parameter Defaults

The neutral `= void 0` formal preserves callable length, but does not recover
the initializer represented by the bytecode. Non-rest callables now obtain
typed ParameterDefault certificates from the common semantic-body pipeline.
There is no source-sidecar input or rewrite of printed JavaScript.

## Admission

The callable explicitly supplies its length for a neutral-default signature,
including ordinary functions, arrows, methods and constructors. The body must
have original instruction coordinates and no catch
or cross-block argument region. Existing IR, CFG, SSA, environment, value and
constructor checks run on the unchanged method before recovery.

Recovery accepts a leading chain of native RegionIf nodes. Each has one arm
and a forward join: load the copied local, save a comparison operand, load
undefined, compare strictly, then skip initialization for a defined value.
The arm loads an integer or undefined and stores it to the same local; a
separately recognized load/save/reload/negate/store arm preserves signed zero.
Only literal initializers are moved. The first default ordinal must equal
the callable length; subsequent ordinals must increase.

The initial copy must read the original formal's entry SSA value. Every read
of that raw formal must be this copy, with no formal writes or phi inputs.
The copied local cannot be observed before its guard. Removed temporary
definitions must have no reads outside their own interval and no phi inputs;
only the final initialized local definition may escape. The earlier prefix
permits scalar transport and lexical-environment setup, not calls or getters.
Later blocks cannot branch back into the initialization prefix. An existing
super-argument owner is allowed only after the complete recovered interval.

The final AST is audited separately: no object fields, expression-only results,
nested owners or origins crossing the removed interval. Entry-binding
substitution separately proves the initialized copy/phi equivalence class;
it is no longer disabled for an entire body with recovered defaults.
Unsupported evidence leaves neutral formals and the complete original body.

## Emission And Positions

The AST replaces the proved interval with same-PC NoOperation nodes, and its
RegionIf printer emits their anchors without an empty conditional. Only a
successfully printed common body returns the certificate to its callable.
The wrapper prints actual literal defaults, retaining `void 0` for undefined
to avoid shadowing by a formal, import or enclosing binding. It does not claim
to recover the original spelling of undefined or distinguish an equivalent
handwritten initializer from a compiler-generated parameter prologue.

The wrapper transfers each removed interval's PC anchors to the initializer
span measured during header emission with CaptureBuffer positions. It removes
the corresponding body anchors before body-coordinate rebasing, so a PC is
not duplicated. Captured templates retain their coordinate cursors through
the same mapping sink. The entire guard/arm maps to the initializer expression,
not to distinct per-operator substrings. No JS text is scanned for positions.

## Cost And Limits

This is conventional literal-prologue recognition plus SSA non-escape checks,
using the existing pinned v7 scalar semantics, not a new dependency. With I
instructions, B blocks, U SSA reads, V values, F phi inputs and P parameters,
recognition is O(I+B+P); indexed use checks are O(U log U +
(U+V+F) log(P+1) + V log(F+1)). Existing SSA queries account for the logarithms.
Scratch is O(B+P); both candidate and retained-default capacities are admitted
against the remaining byte budget. Position transfer costs O(S log(P+1)+I_d)
for S body spans and I_d recovered instructions and allocates no scratch.
This does not prove process-wide peak memory compliance.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include undefined/null/zero/provided arguments, negative zero,
integer limits, skipped default ordinals, modified or escaping raw formals,
observed scratch values, phi uses, backward edges, exceptional/default calls,
owner overlap, scope shadowing, origin relocation, budgets and 200/201 scaling.
Nonliteral defaults, normalized async/catch bodies, local renaming and
capture-to-this recovery remain separate work.

## Production Evidence

Both production CLI targets built. ABC-only pasteboard export recovered
`elmtId = -1, paramsLambda = void 0` in all six page constructors and removed
their two body-initialization conditionals. The subsequent unified entry-binding
pass now removes the proved parameter copies and their unused declarations.
Home method 5851 PCs 42 and 51 now map to the two-character `-1` initializer;
PC 65 maps to the six-character `void 0` initializer on the same signature.
These coordinates were inspected in the generated method-position artifact.

The export still contains eight incomplete sources, two refused constructors
and nine metadata-only records, with 195 methods and 9,745 position entries.
The latest artifact replaces out/pasteboard-latest. Read-only implementation
and mapping review found no concrete defect; paused runtime/regression tests
remain necessary before claiming full semantic equivalence.
