# Local module reads

The pinned v7 `LDLOCALMODULEVAR` handler reads a live local module slot and
`THROW_UNDEFINEDIFHOLEWITHNAME` checks its Hole state. The straight-line
function planner now emits `$localN` only when the load is immediately followed
by that accumulator guard and the enclosing record writer certifies slot `N`
among the export bindings it will declare on `STMODULEVAR`. The JS lexical
binding stays live across later writes. Its temporal dead zone represents a
read before the first store; the guard's name operand matters only on the
exceptional Hole path and is not printed as an expression.

Detached functions, missing record module information, out-of-range slots and
unguarded loads are rejected. The certificate is typed (`module_slots` in the
source scope), not inferred from a printed identifier. External imports keep
their separate request-index validation. `ValidateGuardedModuleLoads` uses two
linear passes, one byte per instruction for admission and bounded work/memory
budgets. Mixed external/local sequences are tested at 200, 201, 400 and 800
loads. A Node oracle checks local live-binding updates and external import
updates. This does not admit module loads in general CFG bodies or imply that
every record using one can now be emitted.
