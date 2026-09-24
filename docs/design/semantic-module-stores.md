# Module stores in the shared AST

`ModuleStore` now lowers to `SourceModuleStore`, retaining its typed local
slot, accumulator input and original PC. SSA input validation still proves
that the stored value is Language. The statement is effectful, is not elided
by copy DCE and does not assign the accumulator. The common statement printer
records its generated span like other source operations.

Printing currently requires the same-record `MethodConstants::module` and
`GlobalScope::module_exports` owner contracts, valid slot bounds, and an
already initialized export slot. It emits an assignment to the existing
`$localN` live binding. It does not mutate the owner's initialization state or
convert imports to writable bindings. A detached method, invalid slot or
uninitialized binding is refused before an artifact is published.

First initialization is intentionally a separate unfinished obligation. A
top-level record owner must prove the declaration site, TDZ lifetime and
control-flow scope; inserting `let` inside arbitrary function/region output
would create the wrong binding. The retained `ModuleExports::WriteStore`
straight-line record path is not called from the generic function printer.
The new node allows shared AST construction to retain module semantics while
record initialization and class ownership are integrated separately.

No per-slot scan or graph pass is introduced. Slot admission and initialized
state lookup are constant time, and output follows the existing byte budget.
Tests/RSS remain paused. Deferred regressions include initialized live updates,
TDZ/missing-owner refusal, slot alias bounds, preserved accumulator, effect
order and source spans in normal and phased bodies. This is not complete
module declaration/export recovery or proof of record-wide equivalence.

ABC-only inspection of pasteboard ProgressBar entry 9915 now passes the
module-store AST node at PC15 and stops at class creation PC264. The command
`abc_semantic ... 9915 --function-source` still refuses output; it does not
claim first-initialization printer admission. Both CLI targets compiled and
read-only review completed; no tests or RSS tracking were run.
