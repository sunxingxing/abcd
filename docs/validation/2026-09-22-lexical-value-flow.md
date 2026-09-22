# Local Lexical Value Categories Validation

## Implemented Boundary

`RegisterValueFlow::BuildWithLexicals` jointly solves register SSA and actually
accessed, nonescaping local cells. Each cell starts with Hole and joins all
explicit stores. Each eligible load receives its cell's categories. Captured
and escaped cells remain Unknown. This is a flow-insensitive cell category
analysis, not reaching-store forwarding, instance equality or initialization
elimination. The original Build entry point keeps its register-only semantics.

The same four-atom finite-height solver handles register/cell cycles. Sorted
typed environment/slot keys avoid per-cell CFG scans and per-declared-slot
allocation. Typed 32-bit graph IDs keep the original compact dependency storage;
combined node counts are checked before conversion. Shared quota and PMR scratch
admit extraction, vector growth, graph construction, propagation and final owned
register-fact publication. Input memory is separately caller-admitted.

## Verification

- Initial local store/load test failed at its category assertion against the
  delegating stub, then passed after joint solving was implemented.
- Complete Debug build succeeded; CTest **75/75 passed**, **35.73 seconds**.
- ASan/UBSan with warnings-as-errors: register_value_flow_test,
  guarded_reads_test and lexical_value_flow_test **3/3 passed**, **60.03 seconds**.
  The new target itself took **29.24 seconds**.
- **500** generated small cyclic CFGs match an independent opcode-specific
  full-sweep oracle with direct slot indexing, rather than the production cell
  index, dependency extraction or queue. This is not an Ark execution oracle.
- Tests cover Hole-only cells, stores after loads, self/cross-cell cycles,
  repeated allocations, captured/escaped exclusions, creation escape,
  deprecated register stores, sparse wide slots and moved owned results.
- A parent and child use the same slot number with different categories; depth
  zero, depth one and pop followed by load preserve the correct owner.
- All **15** nonempty entry-category combinations survive local store/load and
  receive only Hole removal at guarded uses. A fresh load after a prior guard
  still includes Hole. Guard success is not assumed to mean Language.
- Scaling uses **200, 201, 4096, 65536** accesses with either one shared cell or
  distinct environments, including long cell/register dependency chains.
  Counters enforce at most four visits per dependency and four category changes
  per node.
- All insufficient work budgets in the initial fixture fail with typed quota
  exhaustion. Exact work/memory budgets pass; one byte below the recorded peak
  fails with ResourceLimit. Invalid binding evidence fails with InvalidLexicals.
- Independent review reported no concrete production correctness/budget issue.
  Its suggested different-category parent/child case was added and verified.
  `git diff --check` passed. The reviewer did not run the tests.

## Memory

After build, Debug and sanitizer processes finished, the isolated command was:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/lexical_value_flow_test
```

Exit **0**, elapsed **7.97 seconds**, peak RSS **119752 KiB = 122626048 bytes**.
The process includes fixture decoding, CFG/SSA, environment flow, escape proof,
joint value solving and guard analysis. It launches no child processes. This
proves the measured synthetic workload runs below the configured 1000000000-byte
address-space cap, not whole-engine or concurrent full-corpus compliance.

## Remaining Goal

No source emitter or sample file was changed. No source coverage increase is
claimed and no redundant full-corpus source scan was run; the latest source
probe remains `tests/corpus/source-block-loops.json`. No complete application
record source file has been recovered. Exact cell lifetimes/value recovery,
source declarations and Hole-check semantics, function wrappers, record output,
the bounded asynchronous service and end-to-end validation remain incomplete.
