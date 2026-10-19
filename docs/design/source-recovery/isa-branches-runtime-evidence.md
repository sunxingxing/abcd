# ISA increment 1a: runtime evidence blocks comparison admission

2026-10-19, baseline `59db43cc`. This is evidence only: no production semantics,
diagnostics, tests or coverage classifications were changed. The requested
comparison projection is not justified by the pinned runtime.

## Pinned handlers

The lock pins ets_runtime at `f04900cf951c66c2ea18b2bab5b591d5336c34b9`
and ets_frontend at `c754cf6300873cdd8cdb71c7340a98abfd885389`.
All 28 requested handlers in
`references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:6935-7046`
contain `LOG_FULL(FATAL) << "not implement"`, followed by dispatch. They do
not compare operands. The families are:

| Families | Widths | Variants |
| --- | --- | ---: |
| JSTRICTEQZ, JNSTRICTEQZ | imm8, imm16 | 4 |
| JEQNULL, JNENULL | imm8, imm16 | 4 |
| JSTRICTEQNULL, JNSTRICTEQNULL | imm8, imm16 | 4 |
| JEQUNDEFINED, JNEUNDEFINED | imm8, imm16 | 4 |
| JSTRICTEQUNDEFINED, JNSTRICTEQUNDEFINED | imm8, imm16 | 4 |
| JEQ, JNE, JSTRICTEQ, JNSTRICTEQ with v8 | imm8, imm16 | 8 |

This is not just an absent C++ interpreter implementation:

* `ecmascript/compiler/interpreter_stub.cpp` declares these assembly handlers
  with `DISPATCH(NOP)` (for example lines 5979-6003).
* `ecmascript/interpreter/interpreter_assembly.cpp:5242-5254` has dispatch-only
  JEQNULL handlers; JSTRICTEQZ likewise has no comparison at 6019-6031.
* `ecmascript/compiler/baseline/baseline_compiler.cpp:3628` onwards has empty
  handlers for these operations.
* `ecmascript/compiler/slowpath_lowering.cpp:783-811` groups all 28 into a
  no-lowering `break` case.

The ISA YAML defines their encodings and conditional property at
`runtime_core/isa/isa.yaml:1727-1796`, but supplies no individual equality
pseudocode. Its jump group also declares `x_none`. It cannot establish an
abstract register comparison with effects/throwing semantics. In particular,
there is no handler evidence that JSTRICTEQZ means strict equality with either
numeric zero or boolean false. Neither interpretation is safe to invent.

These observations are source evidence, not an execution test that all engines
reject these instructions identically. Do not classify all 28 as universally
non-executable: the execution paths themselves differ. A constructed-ABC test
should expect a profile-specific typed refusal until executable semantics are
established. No source-language premise should silently replace fatal or absent
runtime behavior with an equality expression.

## Dynamic compiler and actual emission

`ets_frontend/es2panda/compiler/core/pandagen.cpp:895-936` emits equality via
`Equal`, `NotEqual`, `StrictEqual`, `StrictNotEqual` then `Jeqz`.
The helpers at 1090-1107 emit `Eq`, `Noteq`, `Stricteq`, `Strictnoteq`.
`BranchIfUndefined`, its strict/negated variants and `BranchIfStrictNull`
(1141-1213) likewise emit separate comparison and JEQZ/JNEZ operations.

The experiment compiled 16 exported conditions (each of `==`, `!=`, `===`,
`!==` against `0`, `null`, `undefined`, another parameter), optional access /
nullish coalescing, and a default parameter. Commands:

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --opt-level=0 --output=/private/tmp/isa-branches-1a/o0.abc /private/tmp/isa-branches-1a/branches.js
"$ES2ABC" --module --merge-abc --output=/private/tmp/isa-branches-1a/default.abc /private/tmp/isa-branches-1a/branches.js
build/regression/isa_coverage_probe /private/tmp/isa-branches-1a/o0.abc
build/regression/isa_coverage_probe /private/tmp/isa-branches-1a/default.abc
```

The production decoder measured:

| Mode | Instructions | EQ / NOTEQ / STRICTEQ / STRICTNOTEQ | JEQZ / JNEZ | Requested variants |
| --- | ---: | --- | --- | ---: |
| O0 | 310 | 4 / 4 / 7 / 6 | 20 / 1 | 0 |
| Default | 182 | 4 / 4 / 7 / 6 | 19 / 2 | 0 |

All those comparison and conditional-branch occurrences use imm8. This bounded
experiment does not prove that no possible frontend input emits the 28 variants.
It supplies no coverage for them and no Node equivalence evidence for them.

SHA-256:

* source: `c43949437707a8d33951fee9e310968a1c2264d85428460b4ca52d3060bdb3cc`
* O0 ABC: `29db72382fe4ec1f5d748aab0a1f418a200cd7c458e778a079c6333e29307046`
* default ABC: `d7dd4173876ddbb41146728541f8d587ef6a23f5a4c50851e69d51f275bf25d8`

## Boundary and recommended revision

Stop before adding comparison IR or changing selection/decision admission.
The separate diagnostic repair remains worthwhile: distinguish an unsupported
branch predicate from a malformed instruction, retain its typed opcode/profile
and original PC, and carry that evidence through both output paths. That repair
and constructed decoding/refusal tests can be a revised increment; they do not
constitute semantic support. Existing JEQZ/JNEZ behavior must remain unchanged.

The 28 ledger rows stay Unmeasured; no oracle pass or class change is claimed.
Full build, fixed regression, sweeps, scaling tests and cohort reruns were not
performed for this evidence-only stop. No production or test source changed.
