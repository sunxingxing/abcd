# Normal Control Flow Implementation Plan

> Execute the approved decompiler architecture incrementally using TDD and
> independent specification/quality reviews. Keep exception flow and source
> recovery explicitly separate from this normal-flow component.

**Goal:** Build bounded typed basic blocks for every decoded method, preserving
conditional alternatives, unreachable code and generator resume boundaries.

**Architecture:** Generate normal transfer categories from the pinned ISA API.
Override generator suspension using pinned interpreter/runtime evidence, not
mnemonic matching at runtime. Build sorted block ranges and at most two normal
successors per block; binary searches resolve PC targets. Mark try/handler
boundaries without expanding the instruction-by-handler product.

**Tech Stack:** C++20, existing decoded bodies, Ruby/ERB ISA generation, CTest.

- [x] Add `abc/control_transfer.h` and generated switch table, with exhaustive
  encoding tests plus explicit return, throw, conditional throw, suspend and
  async-generator resolve checks. Unknown opcodes return typed absence.
- [x] Add `analysis/normal_flow.h` and `src/analysis/normal_flow.cc`:
  `BuildNormalFlow(const abc::MethodBody&, FlowBudget) -> FlowResult`.
  Blocks retain first instruction/count and `array<FlowEdge,2>` successors;
  edges distinguish taken, fallthrough and resume. Returns and unconditional
  throws have no normal successor. Reject malformed bodies and fall-off-end.
- [x] Test diamond, loop, shared conditional target, unreachable instructions,
  suspend/resume, overlapping exception boundaries, invalid targets, empty
  input, budget exhaustion and 200/201/geometric scaling before implementation.
- [x] Bound all graph allocations, including leader scratch space and transient
  reserve storage. Exact allocation after counting avoids growth duplication.
  Complexity is O(I + (J+R+H) log I + B log B), memory O(I+B), with I instructions,
  J jumps, R try ranges, H handlers and B blocks. No per-instruction handler scan.
- [x] Add manifest-backed `abc_flow` validation mode to the existing driver.
  Retain only one method body and graph at a time under the 1 GB process cap.
  Run every dynamic method and report all failures without truncation.
- [x] Run Debug/Release and ASan/UBSan tests, independent reviews, then commit
  full-corpus counts and RSS. This is not an exception-aware CFG or a source
  decompiler; exception dispatch, reference recovery, SSA and output remain.

## Evidence and Semantic Boundary

Pinned runtime_core `isa/isapi.rb` provides `jump?`, `conditional?`,
`is_return_instruction?`, `is_unconditional_throw_instruction?`.
Pinned ets_runtime `interpreter/interpreter-inl.cpp` returns to its caller for
SUSPENDGENERATOR, DEPRECATED_SUSPENDGENERATOR and ASYNCGENERATORRESOLVE.
`RuntimeStubs::SaveFrameToContext` stores the next instruction PC; a resume edge
means a possible later activation, never immediate fallthrough. Completion may
prevent that activation and must be handled during generator semantic recovery.
`asyncfunctionawaituncaught` dispatches normally and is not itself suspension.
ISA `x_none` annotations are not proof that dynamic operations cannot throw.
