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

## Root scope before synthetic labels

The generic forward-region layout originally nested the entire entry block
inside synthetic labels. A first module store there cannot become a module
lexical declaration merely by spelling `let`: the declaration would have the
label's block scope. Hoisting an initialized binding would instead destroy TDZ.

The shared printer now emits the unconditional entry prefix before those
labels. Admission requires no catch regions, entry first in region order,
and no CFG edge back to entry. The final entry-block instruction remains in
the region, keeping its control transfer within the labels. Prefix extraction
stops before a staged call or argument-region property boundary. Ordinary
instruction evaluation order is unchanged; labels themselves have no effects.
Subsequent region printing skips exactly the already emitted prefix. This is
layout only: no statement is erased from the AST, no initialization state is
invented and no first module store becomes legal in a function body.

Spans are recorded at actual emission positions, including the root prefix;
there is no text-based PC reconstruction. The extra scan is O(B + E + P) for
blocks, edges and the candidate prefix, using constant additional storage.
Loop entry and exception layouts remain unchanged. This prepares root-scope
declarations for a record owner, not nested declarations in the existing
standalone function wrapper.

ABC-only inspection of ProgressBar entry 9915 selects 14 prefix statements,
PC0 through PC37, leaving control transfer PC40 inside the region. Printing
still refuses first module initialization at PC15. Production CLI builds and
read-only review completed; no new complete source is claimed. Tests and RSS
tracking remain paused. Deferred regressions include entry backedges, catches,
staged calls, fused/zero-width spans, and exact-once side-effect ordering.
