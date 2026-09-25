# Constructor Argument Recovery

Status: implementation design, not an implemented optimization or equivalence
claim. This extends the approved common IR/region/AST/printer pipeline. It does
not introduce a record-specific writer or change superclass admission.

## Current Evidence

At commit 4600f6c, `BuildSourceAst` represents ConstructorCall as an accumulator
SourceAssignment. Its common printer reads the constructor and argument
registers directly. `FoldArgumentExpressions` visits only SourceStagedCall
owners backed by ReceiverCall; admitting ConstructorCall as an argument RHS in
HasArgumentValue does not recover that constructor's own arguments.

ABC-only inspection of pasteboard method 7089 (PageA Builder callback) shows:

- PCs 22/24/28 load, check and save the constructor into r8.
- PCs 30 through 41 select a captured value; PC 43 saves the first argument
  into r9. This selection crosses basic blocks.
- PCs 45/46 create and save an empty object into r10; 48/49 save undefined
  into r11; 51/53 copy the formal into r12; 55/60 create and save a closure
  into r13.
- PCs 62/66/68/70 create a buffered object and copy it through r15 into r14.
- PC 72 constructs with the seven-register input window beginning at r8:
  the constructor plus six arguments. PC 76 saves its result into r6.

The current source artifact is
`build/pasteboard-label-continuations-final-20260925/sources/entry/src/main/ets/pages/PageA.incomplete.mjs`.
It retains this preparation as statements followed by
`new ($r8)($r9, $r10, $r11, $r12, $r13, $r14)`.
The original IR register effect reads the entire input window and writes the
accumulator. ReceiverCall's argument-read suffix assumption must not be copied
without verifying this different effect layout.

## Design Choice

Use typed argument ownership shared with call recovery, extended to construction.
Do not add a text rewrite, expand the pure-copy super protocol to allocations,
or embed preparation in a new synchronous IIFE. Those approaches respectively
lose provenance, fail the real sample, or introduce an unnecessary execution
scope. Existing conservative register-based construction remains the fallback.

Separate two certificates:

1. An evaluation certificate owns an ordered instruction interval and an
   argument partition. It preserves every evaluation and private commit.
2. A value certificate may remove a particular private commit only after
   exact SSA-use validation, including phi uses and surviving origin chains.

Recovering native argument syntax must not imply dead-store elimination.
Initially preserve commits inside comma-expression arguments where necessary;
elide them only with the second certificate. Keep precomputed branch-selected
arguments outside the first straight-line owned interval. Subsequently admit
the existing conditional-value region under the same ownership rules, without
cloning effects or adding a second owner for its instructions.

## Required Invariants

- Keep the constructor operand's value identical to the original input SSA
  value. Reading a private register before argument preparation is permissible
  only if the interval cannot write that register. Moving its original global,
  module or property lookup requires a separate effect-order proof.
- Native `new` evaluates arguments before checking constructibility. Preserve
  argument side effects and thrown values when the constructor is invalid;
  do not insert a pre-argument constructibility guard.
- Every argument boundary must produce the exact original call-read value.
  If a later interval writes an earlier argument register, retain preparation
  outside the expression or reject this layout. Monotone distinct argument
  targets alone do not rule out hidden writes in nested owners.
- An effectful RHS stays in the original relative order. Preserve closure
  creation PC, capture-domain proof, naming and object literal provenance.
  Do not reconstruct a fresh object for each use of an existing object value.
- One owner prints each instruction. Nested owners must be properly contained
  and certified bottom-up; crossing intervals retain the existing layout.
  Branch, loop, exception and staged-call boundaries cannot be crossed merely
  because the final constructor call is in the same printed region.
- Do not extend to phased SSA or handlers until partial argument commits and
  exception-visible state are proved. Current no-catch admission is not a
  general exception-equivalence proof.
- Retain the original call's result commit/return behavior. SuperRange and
  the two refused host constructors remain governed by their existing proofs.

## Integration Order

1. Introduce a typed construction-argument certificate in the source AST with
   InstructionId ownership, argument ordinals and explicit layout state. Keep
   ConstructorCall as the original semantic operation; use no semantic strings.
   Isolate proof code in `src/source/constructor_arguments.{h,cc}` and register
   it in CMake. Charge certificate storage by actual capacity before publishing
   it. No eager expression-tree copies or unbounded per-call vectors.
2. Build candidate intervals in a single per-block scan after existing result
   retargeting and known-reload folding. Validate SSA call inputs against the
   original constructor window. Track private writes once per interval; do not
   rescan a block for every argument. Reject overlapping ownership explicitly.
3. Integrate argument emission into the common printer. Visit owned operations
   at their original PCs, preserve statement-form restrictions and slot census,
   and attach their spans to emitted arguments. Do not print prefix anchors
   twice or infer offsets from generated text. Call and saved-result spans must
   continue to denote the invocation and its original commit.
4. Extend exact definition tracing from argument_expression.cc to the typed
   constructor input view, including erased load/save pairs. Keep normal-call
   and constructor operand conventions explicit. Remove commits individually,
   then certify a wholly value-only argument list for nesting.
5. Extend region ownership to the first conditional argument in method 7089
   only after the straight-line suffix works through this shared pipeline.
   Preserve branch PCs and the evaluation boundary between constructor lookup
   and argument selection. No method ID or record name enters engine logic.

For I instructions, U SSA reads and A arguments, target O(I + A log(I + U))
proof work and O(I + A) bounded certificate/index space, counting nested
intervals once. If a proof requires repeated suffix scans, redesign the index
before enabling it on production inputs. This uses existing in-tree SSA,
region and effect analyses; no new dependency or licensing obligation.

## Verification Boundary

During the user-requested test pause, build both production CLI targets and
inspect a fresh ABC-only pasteboard export and its PC positions. In method
7089 require the six original argument values, preserved closure/literal PCs,
one construction at PC 72 and result storage at PC 76. A shorter file or
unchanged span count alone is insufficient evidence.

Deferred regressions: invalid constructor with throwing/effectful arguments;
callee overwritten by preparation; later writes to earlier argument slots;
zero and wide ranges; nested/crossing owners; live and phi-used commits;
buffer-copy chains; capture/name preservation; conditional first arguments;
handler-visible partial state; exact position ranges; memory-budget rejection;
200/201 and geometric scaling. Tests, generated JS execution and RSS tracking
remain paused. Full application equivalence and the 1G process limit remain
unverified, and exported records must remain explicitly incomplete.
