# Register Value Categories

## Boundary and Domain

Analyze an owned RegisterSsa together with the unchanged validated body from
which it was built. Classify possible normal-completion VM values, not source
types, expression purity or source equivalence. Four typed atoms form a finite
powerset: language value, Hole, internal environment, other internal value.
Bottom is no established value and is NEVER a language-value proof. Top includes
all four atoms. Unspecified method entries are top; caller-supplied entry facts
must name entry-origin SSA values, be nonempty and unique. Their soundness is a
caller precondition, not inferred from a register's spelling or frame position.

Copy instructions preserve the full set. Phi nodes union all normal inputs,
including method-entry operands. Constants are language values; LDHOLE is Hole;
NEWLEXENV writes an internal environment. Current environment and lexical loads
remain top without scope/cell facts. Function creation produces a language
function on normal completion; that fact does not prove wrapper/home/capture
equivalence. Calls remain top without callee proofs. Successful ordinary
property reads are language values under the pinned v7.0 GetProperty contract:
missing properties return undefined, while getter/proxy abrupt completion is
retained as a possible throw. Receiver/key operands are still checked as
language values; this does not prove field identity or heap immutability.
Selected boolean/string-producing scalar operations have language results when
all operand categories are language-only; otherwise top, or bottom until each
dependency has evidence. Numeric results need stronger numeric facts: accepted
noncanonical NaNs can become impure tagged encodings under negation. FLDAI raw
bits at or above the pinned impure-NaN threshold remain top, not language values.
`MOD2` is a narrow exception: the pinned v7 fast and slow paths canonicalize
NaN before constructing a tagged result, finite `fmod` stays finite, and the
BigInt path returns a language value. Only with language-only operands does
its normal result receive a language fact. The pinned `RuntimeNot`,
`RuntimeShl2`, `RuntimeShr2`, `RuntimeAshr2`, `RuntimeAnd2`, `RuntimeOr2`,
and `RuntimeXor2` paths also return int32, finite uint32, or BigInt on normal
completion; mixed BigInt/Number inputs throw. Their results are language
values only when operand facts are language-only. Other numeric operators
remain conservative.
This admits the paired 12.0.2 `CardItem` constructor's remainder-based array
index without broadening unrelated arithmetic results.

String literals carry an additional must-be-string refinement within the
language atom. Copies and Phi joins retain it only when all incoming values
are strings. `ADD2` returns a string fact only when both inputs are proven
language values and at least one is definitely a string; the pinned v7 slow
path then performs string concatenation after ToPrimitive. Numeric or unknown
additions remain top. This does not admit the Immersive `WindowUtil` property
addition: both operands are unconstrained property reads in ABC, regardless
of the paired TS type annotations.
Integer literals also carry a must-be-non-NaN-number refinement. Copies and
Phi joins retain it only when every incoming value has it. The pinned `NEG`
int32 path produces an int32 or finite double (including `-INT32_MIN`), so
`NEG` preserves this refinement; unconstrained floating NaNs remain top.
Unknown inputs never become language values merely because an ISA writes acc.

Finite nonzero FLDAI literals now provide FiniteNonzeroNumber, signed zero and
infinities provide NonNaNNumber, and accepted NaN payloads remain Language.
Classification uses raw IEEE binary64 magnitude bits, so it never quiets a
signaling payload during analysis. The impure-NaN threshold check still runs
first. Both normal and phased propagation use these same rules and joins.

`DIV2` has a bounded numeric rule: both operands must be NonNaNNumber and at
least one must be FiniteNonzeroNumber. The pinned FastDiv zero-divisor path
produces infinity for a nonzero numerator; otherwise raw division cannot meet
zero/zero or infinity/infinity under these premises. Overflow, underflow and
signed zero retain NonNaNNumber. The result loses the finite/nonzero fact;
Phi joins cannot preserve it if any incoming value is zero, infinity or only
Language. This normal-result proof preserves original user-code conversion
effects, exception arcs and Commit ownership; it does not infer numeric facts
from source annotations, parameter names or property reads.

Unconstrained division remains Unknown. Actual pasteboard EntryAbility method
5165 (`updateBreakpoint`) reads a property and parameter, divides at PC41,
then fails phased input validation on the PC44 copy. Neither operand has the
required numeric fact in ABC. Accepted signaling payload
`0xfff7000000000005` divided by 1 can quiet to `0xffff000000000005`, whose
tagged encoding is Hole. A native macOS probe reproduced this on 2026-10-01;
it was not execution of an Ark VM. The decoded regression preserves rejection
in normal and phased paths and independently covers safe integer/float pairs,
signed zero, infinity, subnormal underflow, mixed Phi inputs and 200/201 chains.

Primary evidence is locked
[FastDiv, lines 43–59](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/interpreter/fast_runtime_stub-inl.h#L43)
and the same commit's
[DIV2 handler, lines 1897–1914](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/interpreter/interpreter-inl.cpp#L1897).
Downloaded fast-runtime header SHA-256:
`00ba9ea1732b1e962e8b1a8b5953b185fe483985bd6036875bc0d952da7213f7`.
The implementation is an independent finite-domain rule, not copied runtime
code. Seven complete common-tree generated quotients pass independent JS
behavior checks; this does not close paired-record semantic/style acceptance.

NaN evidence is pinned with the repository reference lock: `interpreter-inl.cpp`
FLDAI (near 1253) bit-casts the raw immediate, and NEG (near 4892) negates the
decoded double; `js_tagged_value.h` constructor (near 119) adds
DOUBLE_ENCODE_OFFSET after a debug assertion, and IsImpureNaN (near 328) tests
`TAG_INT - DOUBLE_ENCODE_OFFSET`. `js_tagged_value_internals.h` defines the
threshold as 0xfffe000000000000 for this layout. With assertions disabled,
0xffff000000000005 plus the offset wraps to Hole (5). Accepted positive NaN
0x7fff000000000005 becomes that impure pattern under NEG. These are source-level
counterexamples, not a claim that a built Ark runtime was executed in tests.

Addition has a separate signaling-NaN counterexample: accepted input bits
`0xfff7000000000005` can quiet during addition by zero to
`0xffff000000000005`, whose tagged encoding is Hole. The native macOS arithmetic
probe reproduced these bits on 2026-10-01; this is not execution of an Ark VM.
`register_value_flow_test` fixes the decoded FLDAI → register copy → ADD2 → return
case: the load is Language, the sum remains Unknown, and source verification
rejects its observable return. A future numeric refinement must distinguish
safe numeric provenance; Language alone cannot discharge this obligation.

The exact locked commit was fetched independently of the different local runtime
checkout. Primary evidence:
[ADD2 handler, lines 1816–1848](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/interpreter/interpreter-inl.cpp#L1816)
and [double construction, lines 119–124](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/js_tagged_value.h#L119).
Downloaded file SHA-256 values are respectively
`a62ef5d2297505835fd672dba2fe586fd7e38fb07271c248062aa2e58978d0d0` and
`5192911948b06839f219668969222c41516f062383173fe1716ecf58876697fe`.
No upstream implementation was copied into production.

Hole checks do NOT globally narrow their input SSA value: a use before the
check, another path or the exceptional outcome could still observe Hole.
Use-site/path refinement and exception execution need separate analyses.

The optional `BuildWithLexicals` entry point adds nonescaping local cell facts
to this same solver; see [local lexical categories](lexical-value-flow.md).
The original `Build` remains register-only. Neither entry point claims an exact
reaching store or globally removes Hole because a guard exists.

## Algorithm and Cost

Use the standard finite-lattice monotone worklist, independently implemented.
Reference: [Clang data-flow introduction](https://clang.llvm.org/docs/DataFlowAnalysisIntro.html),
read 2026-09-22; no third-party implementation is imported. Prefer an SSA
dependency graph to per-register CFG sweeps. Construct outgoing dependencies
as CSR in linear time. Copies/Phis receive changed source facts incrementally,
never rescan a Phi's complete input list on every update. Unary/binary rules
inspect at most two fixed operands. Each value gains at most four atoms, so
each outgoing edge is visited at most four times. No recursive traversal.

Let V be SSA values, D actual dependencies, F supplied facts, I method size.
Rule extraction uses existing indexed SSA read/write/Phi queries and reviewed
typed lifts: O((V+D) log(2+I+V+D)+F). CSR construction and solving are O(V+D+F),
with a fixed four-atom height plus one string-refinement loss; output storage
O(V), scratch O(V+D). The shared
method quota charges extraction, adjacency construction, propagation and copy.
PMR admits vector growth overlap and final owned output while scratch is live.
The caller separately admits retained input body/SSA bytes and other tasks.

## Implementation Plan

1. Add failing `tests/unit/analysis/register_value_flow_test.cc`: LDHOLE -> STA
   -> LDA must stay Hole, not become a language value. Add its CMake target and
   run the target before implementing the header.
2. Add `value_kinds.h` for the closed typed domain and
   `register_value_flow.h/.cc` for owned results and bounded sparse propagation.
   Keep instruction-rule classification in `register_value_rule.h/.cc` under
   src/analysis so it is not a generic public semantic API.
3. Test constants/copies/merges/loops/unknown calls and loads, exact supplied
   entry facts, non-refinement at Hole guards, internal-environment copies,
   malformed origin/fact boundaries, moves/lifetimes and budget failures.
4. Compare small generated CFGs against an independent slow fixed-point oracle.
   Exercise 200/201 and geometric chains/high-fan-in Phis with work counters;
   run full Debug and relevant ASan/UBSan, request review, measure RSS under a
   1G cap, record evidence and commit. Do not count this as recovered source.
