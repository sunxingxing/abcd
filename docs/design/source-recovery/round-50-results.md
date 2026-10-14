# Round 50 results — property operations

Baseline: `bdab7660`. Workspace implementation, not committed. No commit, push
or stash was performed. No holdout, full test suite or RSS benchmark was run.
Design/proof boundaries: [strict-property-operations.md](strict-property-operations.md).

## Changes

1. `property_delete.{h,cc}`, semantic/register/value adapters, DeleteExpr,
   evaluation/tree operands/structure/recovery and printing implement strict
   native delete. It is not a member read. Boolean acc commits only on success.
2. `indexed_write`, ValueIndexedWrite and LiteralPlan retain a distinct
   `IndexedDefinition::kDefineField` tag. Existing named and indexed operation
   alternatives carry the typed pool-string vs uint32 keys; both use the same
   private object construction proof. Acc is preserved, descriptor flags are
   all true. The new kind cannot use array/staged-assignment projections.
3. `named_read` retains `PropertyLookup::kSuper` in Value IR and MemberExpr.
   Resolved class context supplies HomeObjectBinding, rechecked at publication.
   Current-this SSA identity and native receiver calls are independently
   verified. Recovery retains home-object lookup, and the printer emits super.
   Ordinary prototype/array-length/optional-access projections cannot confuse
   this with an ordinary read on this.

No source contract predicate, production cap, naming or declaration rule changed.

## Actual verification

| Command/slice | Result | Evidence |
|---|---:|---|
| `cmake --build build/regression -j 6 -- -k` | exit 0 | `/private/tmp/r50-build-final.log` |
| `ctest --preset source-regression -j 1` | 65/65 | `/private/tmp/r50-fixed.log` |
| New/extended tests below | 9/9 | `/private/tmp/r50-new-tests-final.log` |
| Related call/member/property/literal/object/exception/recovery tests, serial | 87/94 | `/private/tmp/r50-targeted.log` |
| Previous flags vs explore48 | 497/620, +2; 0 regressions, 0 text drift | `/private/tmp/r50-cohort-final.log` |
| `--language-source` vs explore48s | 534/620, +2; 0 regressions, 0 text drift | same |
| Newly complete outputs, both flags, `node --check` | 4/4 files | o20/7614 and o32/19562 |

New/extended tests: `phased_properties_test`, `property_delete_test`,
`property_delete_tree_test`, `indexed_write_test`, `indexed_define_test`,
`super_read_test`, `property_delete_cli_test`, `indexed_define_cli_test`,
`super_read_cli_test`. CMake registrations are explicit. Fixtures contain
compiler commands/hashes and Node source oracles; target opcodes and real
handlers are asserted before differential execution. The three CLI oracles
exercise 44 delete, 24 indexed-definition and 44 super scenarios across the
normal/phased and two flag sets, plus mutations.

The exact related-test selector was:

```
ctest --test-dir build/regression \
  -R '(delete|indexed|super_read|call|member|property|literal|object|exception|recovery)' \
  --output-on-failure -j 1
```

Its seven failures are all listed as pre-existing in status.md:
`literal_construct_test`, `mixed_literal_test`, `phased_literal_test`,
`literal_closure_source_test`, `unguarded_literal_closure_source_test`,
`derived_literal_receiver_source_test`,
`unguarded_derived_literal_receiver_source_test`.
Their tests/assertions were not changed. This is not a fresh old-checkout test
run and is not a claim that all targeted tests passed.

Final exported evidence is under `/private/tmp/r50-cohort-final/explore50` and
`explore50s`; the runner is `/private/tmp/r50_cohort_final.py`. Every record ID
set, complete status and previously emitted source bytes was compared against
the supplied explore48/explore48s manifests and files. Comparison stops on a
regression or text change. Only the two listed records gain complete status in
each flag set.

## Target records

The result is the same under both flag sets. PCs below are both original and
working PCs (equal here).

| Record | Result | Next typed refusal/location |
|---|---|---|
| o32/21089 core | incomplete; delete passed | `PhasedMethodError=0 > SemanticError=1`, m21553 PC47, GETPROPITERATOR (102) |
| o32/53454 MockKit | incomplete; delete passed | `PhasedMethodError=0 > SemanticError=1`, m54047 PC230, GETPROPITERATOR (102) |
| o32/19562 Constant | complete | none |
| o20/7614 CustomAniamtionTabController | complete | none |

`abc_semantic --instructions` independently confirms the two new first opcodes.
For-in remains excluded; no workaround was added to make these records complete.

## Review focus / limits

- Home-object identity is closed by ClassTreeContext and native class publication,
  not by the spelling `super` or a generic method flag. Nested lexical-super
  arrows remain refused. Review context-to-tree-to-publication correspondence.
- Recovery distinguishes the super lookup base from its this receiver. Review
  home/prototype effects and the guard/success/catch edges, together with the
  independent receiver check and native CallPlan reference identity.
- Indexed DefineField is intentionally an object literal-only projection.
  External objects, arrays and staged assignment forms remain refused.
- Strict delete currently prints computed-key syntax, including literal string
  keys. This is faithful; converting safe strings to dotted spelling is optional.
- The phased array-return delete fixture remains refused at EvaluationError=2;
  it is a negative boundary test, not a success or a proven baseline literal
  regression. No unrelated literal admission was loosened.
- Node source differential is separate from an Ark VM execution. No runtime
  exception-message/stack equivalence or new source-language premise is claimed.
