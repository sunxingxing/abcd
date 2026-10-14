# Property enumeration — Round 51 working boundary

The approved target is native `for (key in object)` with no new source-language
premise. **The current working change implements only raw semantic lifting and
import. It does not implement the approved protocol projection or complete
Round 51.** No source printer admission follows from these raw operations.

## Raw operations implemented

`PropertyIterator` reads the accumulator and produces a distinct internal
`PropertyEnumerator` value. `NextPropertyName` reads its enumerator operand and
produces a language value. Neither operation is pure: heap reads/writes,
allocation and user code are conservatively retained both on success and before
throw. The accumulator is defined only on successful completion; the phased
import attaches that definition to Commit.

The pinned references are `OpenHarmony-v7.0-Release`, as recorded in
`docs/reference/openharmony-v7.0-release.lock.json`:

- `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`: GETPROPITERATOR at
  1797 and GETNEXTPROPNAME at 2574 check abrupt completion before updating acc.
- `ets_runtime/ecmascript/js_object.cpp:3006`: EnumerateObjectProperties.
- `ets_runtime/ecmascript/js_for_in_iterator.cpp:55`: mutation/deletion checks;
  `:84`: NextInternal and its undefined exhaustion result.
- `ets_runtime/ecmascript/property_accessor.cpp:388`: property/prototype key
  collection, including proxy handling.
- `ets_frontend/ets2panda/compiler/core/JSCompiler.cpp:1362`: native for-in
  lowering uses acquisition, advance and the undefined test.

The new internal domain may pass through ordinary register copies or the
enumerator input of `NextPropertyName`. It is excluded from language values,
Hole transport, source phi copies, returns and observable operands. Next is
deliberately not classified as String-only: the pinned proxy slow path does
not establish that refinement. Import does not prove closed use, loop
ownership, dynamic acquisition identity or source variable lifetime.

## Remaining approved work

The source route still refuses `ValueEnumeration`. The following are required
before emitting any native for-in:

1. An indexed protocol proof over SSA and original control: dynamic acquisition
   identity, copy/phi identity, closed enumerator use, single advance per
   iteration, done equivalence on the next-result domain, key binding/live-out,
   and acquisition-to-first-next effect order.
2. A general exception-forwarding summary, consumed only by this projection.
   It must prove the thrown value, dispatch and observable state from the graph;
   STA/LDA/THROW adjacency is not evidence. Acquisition-only handlers must not
   be expanded to cover loop advancement or body effects.
3. Native ForIn tree representation and activation-aware key binding, with
   independent tree/recovery checks of event membership, effects, exceptions,
   original edges and binding activation. The two earlier MockKit index/delete
   loops must retain their existing plans.
4. Protocol-specific typed refusals and the real es2abc/Node integration
   fixture, including forged proofs, escapes, wrong completion, binding
   lifetime, and control/exception mutations.

Proof construction must allocate no source symbols or names, charge all
buffers before allocation, and remain within O(N log N). Existing complete
methods and their catches must not be replanned.

## Runtime verification boundary

The premise-free projection targets the pinned Ark runtime: es2abc lowers
native for-in to these same operations. Node differentials can check ordinary
objects, ordinary key order, control/return traces and exception constructors.
They do **not** establish equality with Ark for Proxy trap sequences, prototype
or enumerability mutation during enumeration, or the Proxy Symbol-key path.
Those cases remain an explicit runtime-verification gap; a source-only oracle
must not be reported as Ark execution evidence. No Node differential of
engine-generated for-in has been completed by this working increment.

## Validation of the raw increment

- `property_enumeration_test`: normal and phased imports, success-only acc
  definition, acquisition-only handler, internal-value escape rejection,
  register/copy/block-split variants, operand/effect mutations, and importer
  memory/work boundaries at 200/201/400/800 operations.
- Fixed source regression: 65/65. Selected loop/exception/iterator/source
  tests: 24/24. Logs: `/private/tmp/r51-final-fixed.log` and
  `/private/tmp/r51-final-targeted.log`.
- Full keep-going build succeeded; log: `/private/tmp/r51-final-build.log`.
- The targeted set was exception_plan, loop_exit, exception_copies,
  loop_syntax, loop_transition, loop_initializer, loop_closure_file,
  loop_forest, value_import, source_route, property_enumeration,
  exception_tree_source, exception_outer_source, loop_condition_source,
  loop_transition_source, loop_initializer_source, loop_exit_source,
  loop_closure_source, register_value_flow, exception_arguments,
  phased_value_import, iterator, exception_dispatch and exceptions_cli
  (all names have `_test`). No failure occurred in this selected set; the
  separately registered pre-existing failures were not changed.
- Both exploration comparisons against explore50/explore50s: 497/620 and
  534/620, zero status changes and zero source-byte changes. These unchanged
  counts are regression evidence only, not for-in completion.
- Comparison outputs: `/private/tmp/r51-final-cohort/explore51{,s}`; comparison log:
  `/private/tmp/r51-final-cohort.log`.

Both flag sets publish the same current first refusals:

| Record | Method | Original/working PC | Current refusal |
| --- | --- | --- | --- |
| o32/29268 assertContain | 29450 | 90/90 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |
| o32/42397 DataDriver | 42717 | 153/153 | PhasedMethodError::kInput > SemanticError::kUnsupportedInstruction (GETITERATOR) |
| o32/43537 Filter, first loop | 44469 | 28/28 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |
| o32/43537 Filter, second loop | 44469 | 179/179 | Raw operation lifted; source projection not independently reached past the first loop refusal |
| o32/21089 core | 21553 | 47/47 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |
| o32/53454 MockKit | 54047 | 230/230 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |

DataDriver's new first refusal is an independent iterator protocol and remains
out of scope. Its change of first refusal does not prove that its for-in source
projection succeeded: family preparation discovers another child before tree
emission. None of the five records is complete in this working increment.
